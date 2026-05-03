// scratch-from-dynamic-indexing
//
// Detects local arrays declared with constexpr size that are then indexed
// by a non-constexpr expression. On most IHVs (RDNA 1+, Turing+, Xe-HPG)
// dynamic indexing into a register-resident array forces the compiler to
// fall back to scratch (stack) memory; the array can't be flattened into
// the VGPR file because the access pattern isn't decidable at compile time.
//
// Stage: Ast. The detection is straightforward: walk every
// `subscript_expression` whose receiver is a function-local array
// declaration AND whose index is not a literal / `[unroll]`-friendly
// constant.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "scratch-from-dynamic-indexing";
constexpr std::string_view k_category = "control-flow";

/// True when the index expression is a compile-time constant (numeric
/// literal or simple integer arithmetic on literals only).
[[nodiscard]] bool is_constant_index(::TSNode index, std::string_view bytes) noexcept {
    if (::ts_node_is_null(index))
        return true;  // missing index node -- conservative: don't fire
    const auto kind = node_kind(index);
    if (kind == "number_literal" || kind == "integer_literal")
        return true;
    const auto text = node_text(index, bytes);
    if (text.empty())
        return true;  // empty -- conservative
    // Accept simple decimal / hex literal with optional `u` / `U` suffix.
    auto trimmed = text;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
        trimmed.remove_prefix(1U);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                                trimmed.back() == 'u' || trimmed.back() == 'U'))
        trimmed.remove_suffix(1U);
    if (trimmed.empty())
        return true;
    // Hex literal `0x...`.
    if (trimmed.size() > 2U && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        for (std::size_t i = 2U; i < trimmed.size(); ++i) {
            const char c = trimmed[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    }
    bool all_digit = true;
    for (const char c : trimmed) {
        if (c < '0' || c > '9') {
            all_digit = false;
            break;
        }
    }
    return all_digit;
}

/// Collect names of locally-declared arrays in the function -- those whose
/// declarator carries a `[N]` array suffix and whose enclosing scope is a
/// function body (not a global or struct field).
void collect_local_arrays(::TSNode node,
                          std::string_view bytes,
                          std::unordered_set<std::string>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "function_definition") {
        // Walk the body looking for declarations with array_declarator.
        std::vector<::TSNode> stack;
        const auto cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            stack.push_back(::ts_node_child(node, i));
        }
        while (!stack.empty()) {
            const auto n = stack.back();
            stack.pop_back();
            if (::ts_node_is_null(n))
                continue;
            const auto k = node_kind(n);
            if (k == "function_definition") {
                continue;
            }
            if (k == "array_declarator") {
                // The receiver (declared name) is the first identifier child.
                ::TSNode inner = ::ts_node_child_by_field_name(n, "declarator", 10U);
                if (::ts_node_is_null(inner)) {
                    inner = ::ts_node_child(n, 0U);
                }
                while (!::ts_node_is_null(inner) && node_kind(inner) == "array_declarator") {
                    const ::TSNode child = ::ts_node_child_by_field_name(inner, "declarator", 10U);
                    if (::ts_node_is_null(child))
                        break;
                    inner = child;
                }
                if (!::ts_node_is_null(inner) && node_kind(inner) == "identifier") {
                    const auto name = node_text(inner, bytes);
                    if (!name.empty()) {
                        out.insert(std::string{name});
                    }
                }
            }
            const auto child_cnt = ::ts_node_child_count(n);
            for (std::uint32_t i = 0; i < child_cnt; ++i) {
                stack.push_back(::ts_node_child(n, i));
            }
        }
        return;  // do not recurse past the function header (walk handled above)
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_local_arrays(::ts_node_child(node, i), bytes, out);
    }
}

void scan_subscripts(::TSNode node,
                     std::string_view bytes,
                     const std::unordered_set<std::string>& arrays,
                     const AstTree& tree,
                     RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "subscript_expression") {
        ::TSNode receiver = ::ts_node_child_by_field_name(node, "argument", 8);
        if (::ts_node_is_null(receiver)) {
            // Fall back to first named child as receiver.
            if (::ts_node_named_child_count(node) > 0U) {
                receiver = ::ts_node_named_child(node, 0U);
            }
        }
        const auto receiver_text = node_text(receiver, bytes);
        if (!receiver_text.empty() && arrays.contains(std::string{receiver_text})) {
            // The grammar exposes `indices` field (a `subscript_argument_list`).
            // Pull the inner expression -- if there's exactly one index, that's
            // our candidate.
            ::TSNode index{};
            const auto idx_list = ::ts_node_child_by_field_name(node, "indices", 7);
            if (!::ts_node_is_null(idx_list)) {
                const auto sub_named = ::ts_node_named_child_count(idx_list);
                if (sub_named == 1U) {
                    index = ::ts_node_named_child(idx_list, 0U);
                }
            }
            // Conservative: if we couldn't extract an index node, do not fire.
            if (!::ts_node_is_null(index) && !is_constant_index(index, bytes)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{"local array `"} + std::string{receiver_text} +
                               "[...]` is indexed by a non-constant expression -- on most "
                               "IHVs the compiler falls back to scratch memory because the "
                               "register file can't be addressed dynamically; consider "
                               "splitting the array into named locals or pulling the data "
                               "from a `Buffer<>` resource";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        scan_subscripts(::ts_node_child(node, i), bytes, arrays, tree, ctx);
    }
}

class ScratchFromDynamicIndexing : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto root = ::ts_tree_root_node(tree.raw_tree());
        std::unordered_set<std::string> arrays;
        collect_local_arrays(root, tree.source_bytes(), arrays);
        if (arrays.empty())
            return;
        scan_subscripts(root, tree.source_bytes(), arrays, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_scratch_from_dynamic_indexing() {
    return std::make_unique<ScratchFromDynamicIndexing>();
}

}  // namespace hlsl_clippy::rules
