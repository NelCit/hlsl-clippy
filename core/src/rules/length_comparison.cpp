// length-comparison
//
// Detects `length(v) < r`, `length(v) > r`, `length(v) <= r`, `length(v) >= r`
// and suggests rewriting as `dot(v, v) < r * r` (and the same for the other
// comparison operators). `length` performs a square root that the comparison
// makes redundant: if `r >= 0`, comparing `length(v)` to `r` is equivalent to
// comparing `dot(v, v)` to `r * r`, and skips the rsqrt + mul.
//
// The fix is suggestion-only because the rewrite changes numeric properties:
// - `r * r` overflows earlier than `r` does for large `r`.
// - For negative `r`, `length(v) < r` is always false but `dot(v, v) < r*r`
//   may be true.
// - The user must verify `r >= 0` (or `r >= 0` is invariant).
//
// We accept `length(v)` on either side of the comparison: `r > length(v)` is
// folded to `length(v) < r` and rewritten the same way (with the operator
// flipped).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

constexpr std::string_view k_rule_id = "length-comparison";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_length_name = "length";

/// Return the operator text of a binary_expression node.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const std::uint32_t count = ::ts_node_child_count(expr);
    for (std::uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        return node_text(child, bytes);
    }
    return {};
}

/// Return the textual argument of `length(v)` if `node` is such a call,
/// otherwise an empty view.
[[nodiscard]] std::string_view length_arg_text(::TSNode node, std::string_view bytes) noexcept {
    if (node_kind(node) != "call_expression")
        return {};
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (node_text(fn, bytes) != k_length_name)
        return {};
    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 1U)
        return {};
    return node_text(::ts_node_named_child(args, 0), bytes);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression") {
        const auto op = binary_op(node, bytes);
        const bool is_cmp = (op == "<" || op == ">" || op == "<=" || op == ">=");
        if (is_cmp) {
            const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
            const ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);

            std::string_view v_text;
            std::string_view r_text;
            std::string_view rewrite_op;

            const auto lhs_v = length_arg_text(lhs, bytes);
            if (!lhs_v.empty()) {
                // length(v) <op> r
                v_text = lhs_v;
                r_text = node_text(rhs, bytes);
                rewrite_op = op;
            } else {
                const auto rhs_v = length_arg_text(rhs, bytes);
                if (!rhs_v.empty()) {
                    // r <op> length(v)  ==>  flip operator so v stays on the left
                    v_text = rhs_v;
                    r_text = node_text(lhs, bytes);
                    if (op == "<")
                        rewrite_op = ">";
                    else if (op == ">")
                        rewrite_op = "<";
                    else if (op == "<=")
                        rewrite_op = ">=";
                    else
                        rewrite_op = "<=";
                }
            }

            if (!v_text.empty() && !r_text.empty()) {
                const auto expr_range = tree.byte_range(node);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
                diag.message = std::string{
                    "`length(v)` compared against a scalar can usually be "
                    "rewritten as `dot(v, v)` against the squared scalar -- "
                    "saves a sqrt at the cost of an earlier overflow point"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "if the right-hand side is non-negative (and `r * r` does not "
                    "overflow), replace with `dot(v, v) <op> r * r`"};
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
                std::string replacement;
                replacement.reserve(v_text.size() * 2 + r_text.size() * 2 + 16);
                replacement.append("dot(");
                replacement.append(v_text);
                replacement.append(", ");
                replacement.append(v_text);
                replacement.append(") ");
                replacement.append(rewrite_op);
                replacement.append(" (");
                replacement.append(r_text);
                replacement.append(") * (");
                replacement.append(r_text);
                replacement.append(")");
                edit.replacement = std::move(replacement);
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class LengthComparison : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_length_comparison() {
    return std::make_unique<LengthComparison>();
}

}  // namespace hlsl_clippy::rules
