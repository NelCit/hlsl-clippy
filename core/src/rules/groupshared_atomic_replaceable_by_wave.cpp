// groupshared-atomic-replaceable-by-wave
//
// Detects `Interlocked{Add,Or,And,Xor,Min,Max}(gs[k], expr)` where the
// destination is a single groupshared cell -- typically a counter at index 0
// -- and a wave-reduce + one-lane atomic would replace 32 (RDNA wave32 /
// NVIDIA / Xe-HPG) or 64 (RDNA wave64) per-lane LDS atomics with one.
//
// Stage: `Ast`. The trigger is a syntactic shape: an `Interlocked*` call
// whose first argument is a subscript of a known groupshared name, indexed
// by a compile-time literal (typically `[0]`). The rule does not need
// uniformity analysis to fire because the wave-reduce idiom is correct any
// time every wave-active lane contributes; what the author chooses to
// replace it with depends on the per-lane operand.
//
// Detection (purely AST, conservative):
//   1. Collect every `groupshared` declaration's variable name.
//   2. Walk the tree looking for `call_expression` nodes whose function
//      identifier is one of the targeted `Interlocked*` intrinsics.
//   3. Inspect the first argument. If it is a `subscript_expression` whose
//      receiver is a recorded groupshared name AND the index is a constant
//      literal (or the receiver is a scalar groupshared declared without
//      `[N]`), fire with the wave-reduce suggestion.
//
// Original-value-out (`InterlockedAdd(gs, val, oldVal)`) is intentionally
// matched too: the wave-reduce rewrite still works, the author just has to
// add a small post-fixup if the per-lane `oldVal` is consumed.
//
// The fix is `suggestion`-only: the correct wave reduction depends on the
// op (`WaveActiveSum` for Add, `WaveActiveBitOr` for Or, ...).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-atomic-replaceable-by-wave";
constexpr std::string_view k_category = "workgroup";

constexpr std::array<std::string_view, 6> k_atomic_calls{
    "InterlockedAdd",
    "InterlockedOr",
    "InterlockedAnd",
    "InterlockedXor",
    "InterlockedMin",
    "InterlockedMax",
};

[[nodiscard]] std::string_view wave_replacement(std::string_view atomic_name) noexcept {
    if (atomic_name == "InterlockedAdd") {
        return "WaveActiveSum";
    }
    if (atomic_name == "InterlockedOr") {
        return "WaveActiveBitOr";
    }
    if (atomic_name == "InterlockedAnd") {
        return "WaveActiveBitAnd";
    }
    if (atomic_name == "InterlockedXor") {
        return "WaveActiveBitXor";
    }
    if (atomic_name == "InterlockedMin") {
        return "WaveActiveMin";
    }
    if (atomic_name == "InterlockedMax") {
        return "WaveActiveMax";
    }
    return {};
}

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

void collect_groupshared_names(::TSNode node,
                               std::string_view bytes,
                               std::unordered_set<std::string>& out_names,
                               std::unordered_set<std::string>& out_scalar_names) {
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
                const bool is_array = (node_kind(declarator) == "array_declarator") ||
                                      (text.find('[') != std::string_view::npos);
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
                    out_names.insert(std::string{name});
                    if (!is_array) {
                        out_scalar_names.insert(std::string{name});
                    }
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_groupshared_names(::ts_node_child(node, i), bytes, out_names, out_scalar_names);
    }
}

[[nodiscard]] bool is_constant_literal_index(::TSNode index) noexcept {
    if (::ts_node_is_null(index)) {
        return false;
    }
    return node_kind(index) == "number_literal";
}

void scan_calls(::TSNode node,
                std::string_view bytes,
                const std::unordered_set<std::string>& names,
                const std::unordered_set<std::string>& scalar_names,
                const AstTree& tree,
                RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        std::string_view matched_atomic;
        for (const auto name : k_atomic_calls) {
            if (fn_text == name) {
                matched_atomic = name;
                break;
            }
        }
        if (!matched_atomic.empty()) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args)) {
                // Find the first non-punctuation child of the argument list.
                ::TSNode first_arg{};
                const std::uint32_t cnt = ::ts_node_child_count(args);
                for (std::uint32_t i = 0; i < cnt; ++i) {
                    const ::TSNode c = ::ts_node_child(args, i);
                    const auto k = node_kind(c);
                    if (k != "(" && k != ")" && k != ",") {
                        first_arg = c;
                        break;
                    }
                }
                if (!::ts_node_is_null(first_arg)) {
                    bool fires = false;
                    if (node_kind(first_arg) == "subscript_expression") {
                        ::TSNode recv = ::ts_node_child_by_field_name(first_arg, "argument", 8);
                        if (::ts_node_is_null(recv)) {
                            recv = ::ts_node_named_child(first_arg, 0);
                        }
                        const auto recv_text = node_text(recv, bytes);
                        if (!recv_text.empty() && names.contains(std::string{recv_text})) {
                            // Tree-sitter-hlsl wraps the index expression in a
                            // subscript_argument_list (field name "indices",
                            // 7 chars). Descend into its first named child to
                            // get the actual index expression.
                            ::TSNode indices =
                                ::ts_node_child_by_field_name(first_arg, "indices", 7);
                            if (::ts_node_is_null(indices)) {
                                indices = ::ts_node_named_child(first_arg, 1);
                            }
                            ::TSNode idx{};
                            if (!::ts_node_is_null(indices)) {
                                if (node_kind(indices) == "subscript_argument_list") {
                                    idx = ::ts_node_named_child(indices, 0);
                                } else {
                                    idx = indices;
                                }
                            }
                            if (is_constant_literal_index(idx)) {
                                fires = true;
                            }
                        }
                    } else if (node_kind(first_arg) == "identifier") {
                        // Scalar groupshared (no subscript): `InterlockedAdd(g_Count, 1)`.
                        const auto recv_text = node_text(first_arg, bytes);
                        if (!recv_text.empty() && scalar_names.contains(std::string{recv_text})) {
                            fires = true;
                        }
                    }

                    if (fires) {
                        const auto repl = wave_replacement(matched_atomic);
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span =
                            Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                        diag.message =
                            std::string{"`"} + std::string{matched_atomic} +
                            "` against a single groupshared cell serialises 32-/64-way " +
                            "across the wave; replace with `" + std::string{repl} +
                            "` + a single representative-lane atomic (gated on " +
                            "`WaveIsFirstLane()`) to collapse the LDS-atomic round trips";
                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_calls(::ts_node_child(node, i), bytes, names, scalar_names, tree, ctx);
    }
}

class GroupsharedAtomicReplaceableByWave : public Rule {
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
        std::unordered_set<std::string> scalar_names;
        collect_groupshared_names(root, bytes, names, scalar_names);
        if (names.empty()) {
            return;
        }
        scan_calls(root, bytes, names, scalar_names, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_atomic_replaceable_by_wave() {
    return std::make_unique<GroupsharedAtomicReplaceableByWave>();
}

}  // namespace shader_clippy::rules
