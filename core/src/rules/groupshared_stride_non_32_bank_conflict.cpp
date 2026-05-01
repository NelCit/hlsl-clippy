// groupshared-stride-non-32-bank-conflict
//
// Detects `groupshared T arr[N]` accesses indexed by a thread-id /
// group-index expression scaled by a compile-time stride S in {2, 4, 8, 16,
// 64} -- strides that share a non-trivial GCD with the LDS bank count of 32
// (RDNA wave32 / NVIDIA / Xe-HPG). The LDS bank-conflict factor for a
// constant stride S against 32 banks is `32 / gcd(S, 32)`, so:
//   * S=2  -> 2-way conflict
//   * S=4  -> 4-way
//   * S=8  -> 8-way
//   * S=16 -> 16-way
//   * S=64 -> 32-way (same as exact-32 case, which is a separate rule)
//
// The companion rule `groupshared-stride-32-bank-conflict` (ADR 0007 Phase 4)
// catches the exact-32 multiple. This rule catches the partial-conflict
// strides above. Stage is `Ast` -- no CFG / uniformity needed: the trigger
// is a syntactic shape (subscript whose index is a binary `*` of a thread-id-
// like identifier and a literal in the target stride set).
//
// Detection (purely AST, conservative):
//   1. Collect every `groupshared` declaration's variable name.
//   2. Walk the tree looking for `subscript_expression` nodes whose receiver
//      is one of the recorded names.
//   3. Inspect the index expression. If it is a `binary_expression` with
//      operator `*` whose operands are (a) a thread-id-like identifier or
//      member access (`tid`, `gi`, `dtid.x`, `gtid.y`, ...) and (b) a number
//      literal whose value is in {2, 4, 8, 16, 64}, fire with the conflict
//      factor.
//   4. Additive offsets (`tid * 4 + k`) are also accepted because the +k is
//      lane-uniform within a wave and does not change the bank-modulo
//      structure.
//
// The fix is `suggestion`-only: padding the inner array dimension or
// converting AoS to SoA depends on the surrounding code shape.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

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

constexpr std::string_view k_rule_id = "groupshared-stride-non-32-bank-conflict";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// Strip leading `+` sign and trailing integer / float / unsigned suffixes,
/// returning the unsigned decimal integer the literal represents, or
/// `std::uint32_t{0}` plus `false` when the literal is not a parseable
/// non-negative integer in the small-stride range we care about.
[[nodiscard]] bool parse_unsigned_literal(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (text[i] == '+') {
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    std::uint64_t value = 0;
    bool any_digit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = (value * 10U) + static_cast<std::uint32_t>(text[i] - '0');
        ++i;
        any_digit = true;
        if (value > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (!any_digit) {
        return false;
    }
    // Allow trailing `u` / `U` / `l` / `L` integer suffixes only.
    while (i < text.size()) {
        const char c = text[i];
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L') {
            ++i;
            continue;
        }
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] std::uint32_t conflict_factor(std::uint32_t stride) noexcept {
    constexpr std::uint32_t k_bank_count = 32U;
    if (stride == 0U) {
        return 1U;
    }
    std::uint32_t a = stride;
    std::uint32_t b = k_bank_count;
    while (b != 0U) {
        const std::uint32_t t = a % b;
        a = b;
        b = t;
    }
    return k_bank_count / a;  // 32 / gcd(stride, 32)
}

/// True when `stride` is a non-trivial-but-not-exact-32-multiple stride that
/// produces a partial bank conflict.
[[nodiscard]] bool is_target_stride(std::uint32_t stride) noexcept {
    return stride == 2U || stride == 4U || stride == 8U || stride == 16U || stride == 64U;
}

/// True if `text` looks like a thread-id-like identifier or member access
/// (heuristic over the canonical names compute shaders use). Conservatively
/// false on unrelated names so that non-thread-id strides don't fire.
[[nodiscard]] bool looks_like_thread_id(std::string_view text) noexcept {
    static constexpr std::array<std::string_view, 12> k_seeds{
        "tid",
        "gid",
        "gi",
        "dtid",
        "gtid",
        "groupIndex",
        "DTid",
        "GTid",
        "GI",
        "GroupIndex",
        "lane",
        "laneIndex",
    };
    for (const auto seed : k_seeds) {
        if (has_keyword(text, seed)) {
            return true;
        }
    }
    // Also accept any text containing a system-value semantic name.
    return has_keyword(text, "SV_DispatchThreadID") || has_keyword(text, "SV_GroupThreadID") ||
           has_keyword(text, "SV_GroupIndex") || has_keyword(text, "WaveGetLaneIndex");
}

/// Walk the tree collecting names of `groupshared` declarations.
void collect_groupshared_names(::TSNode node,
                               std::string_view bytes,
                               std::unordered_set<std::string>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        if (has_keyword(text, "groupshared")) {
            const ::TSNode declarator = ::ts_node_child_by_field_name(node, "declarator", 10);
            if (!::ts_node_is_null(declarator)) {
                ::TSNode name_node = declarator;
                if (node_kind(declarator) == "init_declarator" ||
                    node_kind(declarator) == "array_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(declarator, "declarator", 10);
                    if (!::ts_node_is_null(inner)) {
                        name_node = inner;
                    }
                }
                if (node_kind(name_node) == "array_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(name_node, "declarator", 10);
                    if (!::ts_node_is_null(inner)) {
                        name_node = inner;
                    }
                }
                const auto name = node_text(name_node, bytes);
                if (!name.empty()) {
                    out.insert(std::string{name});
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_groupshared_names(::ts_node_child(node, i), bytes, out);
    }
}

/// Try to extract a multiplicative stride from an index expression. Returns
/// 0 when the expression does not match `<thread-id-like> * <literal>` or
/// `<literal> * <thread-id-like>` (optionally wrapped in additive offsets).
/// Walks down `+` / `-` binary expressions to find the multiplicative leaf.
[[nodiscard]] std::uint32_t extract_stride(::TSNode index, std::string_view bytes) {
    if (::ts_node_is_null(index)) {
        return 0U;
    }
    const auto kind = node_kind(index);

    if (kind == "binary_expression") {
        // Read operator field; tree-sitter-hlsl uses field "operator".
        const ::TSNode op = ::ts_node_child_by_field_name(index, "operator", 8);
        const auto op_text = node_text(op, bytes);

        const ::TSNode left = ::ts_node_child_by_field_name(index, "left", 4);
        const ::TSNode right = ::ts_node_child_by_field_name(index, "right", 5);

        if (op_text == "*") {
            // Either order: id * lit OR lit * id.
            std::uint32_t lit = 0U;
            const auto left_text = node_text(left, bytes);
            const auto right_text = node_text(right, bytes);
            const bool left_is_lit =
                node_kind(left) == "number_literal" && parse_unsigned_literal(left_text, lit);
            std::uint32_t lit_r = 0U;
            const bool right_is_lit =
                node_kind(right) == "number_literal" && parse_unsigned_literal(right_text, lit_r);
            if (left_is_lit && looks_like_thread_id(right_text)) {
                return lit;
            }
            if (right_is_lit && looks_like_thread_id(left_text)) {
                return lit_r;
            }
            return 0U;
        }
        if (op_text == "+" || op_text == "-") {
            // Recurse into both sides; the multiplicative leaf wins.
            const std::uint32_t l = extract_stride(left, bytes);
            if (l != 0U) {
                return l;
            }
            return extract_stride(right, bytes);
        }
        if (op_text == "<<") {
            // `tid << B` is equivalent to `tid * (1 << B)`.
            const auto right_text = node_text(right, bytes);
            std::uint32_t shift = 0U;
            if (node_kind(right) == "number_literal" && parse_unsigned_literal(right_text, shift)) {
                if (shift < 32U && looks_like_thread_id(node_text(left, bytes))) {
                    return 1U << shift;
                }
            }
            return 0U;
        }
        return 0U;
    }
    if (kind == "parenthesized_expression") {
        // Tree-sitter often wraps in a paren node.
        const std::uint32_t cnt = ::ts_node_child_count(index);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const std::uint32_t s = extract_stride(::ts_node_child(index, i), bytes);
            if (s != 0U) {
                return s;
            }
        }
    }
    return 0U;
}

void scan_subscripts(::TSNode node,
                     std::string_view bytes,
                     const std::unordered_set<std::string>& names,
                     const AstTree& tree,
                     RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "subscript_expression") {
        // Tree-sitter-hlsl exposes "argument" / "indices" fields on
        // subscript_expression. The "indices" field is a
        // subscript_argument_list wrapper around the actual index expression
        // (between `[` and `]`). Use named-child positional fallback for
        // robustness against partial-grammar paths.
        ::TSNode receiver = ::ts_node_child_by_field_name(node, "argument", 8);
        if (::ts_node_is_null(receiver)) {
            receiver = ::ts_node_named_child(node, 0);
        }
        ::TSNode indices = ::ts_node_child_by_field_name(node, "indices", 7);
        if (::ts_node_is_null(indices)) {
            indices = ::ts_node_named_child(node, 1);
        }
        // Descend into subscript_argument_list to get the actual index
        // expression (its first named child).
        ::TSNode index{};
        if (!::ts_node_is_null(indices)) {
            if (node_kind(indices) == "subscript_argument_list") {
                index = ::ts_node_named_child(indices, 0);
            } else {
                index = indices;
            }
        }
        const auto receiver_text = node_text(receiver, bytes);
        if (!receiver_text.empty() && names.contains(std::string{receiver_text})) {
            const std::uint32_t stride = extract_stride(index, bytes);
            if (stride != 0U && is_target_stride(stride)) {
                const std::uint32_t factor = conflict_factor(stride);
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{"groupshared access at stride "} +
                               std::to_string(stride) + " produces a " + std::to_string(factor) +
                               "-way LDS bank conflict on 32-bank GPUs (RDNA / NVIDIA / "
                               "Xe-HPG); restructure AoS to SoA or pad inner dimension to "
                               "be coprime with 32";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_subscripts(::ts_node_child(node, i), bytes, names, tree, ctx);
    }
}

class GroupsharedStrideNon32BankConflict : public Rule {
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
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        const auto bytes = tree.source_bytes();

        std::unordered_set<std::string> names;
        collect_groupshared_names(root, bytes, names);
        if (names.empty()) {
            return;
        }
        scan_subscripts(root, bytes, names, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_stride_non_32_bank_conflict() {
    return std::make_unique<GroupsharedStrideNon32BankConflict>();
}

}  // namespace hlsl_clippy::rules
