// manual-reflect
//
// Detects hand-rolled implementation of `reflect(v, n)`:
//
//     v - 2 * dot(n, v) * n
//     v - 2.0 * dot(n, v) * n
//     v - dot(n, v) * 2 * n    (commutative reorderings)
//     etc.
//
// The general form is: `v - <scalar_2> * dot(n, v) * n`
// where `<scalar_2>` is the literal 2 or 2.0 (or with float suffix),
// and both occurrences of `v` and `n` must be textually identical
// (i.e., the same simple identifier or field_expression).
//
// The fix is SUGGESTION-ONLY because the incident-vs-normal argument ordering
// convention differs between codebases, and callers should verify the semantics.
//
// We match via on_tree imperative walk: look for binary_expression with
// operator `-` whose right subtree contains the reflect pattern.

#include <cstdint>
#include <memory>
#include <optional>
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

constexpr std::string_view k_rule_id = "manual-reflect";
constexpr std::string_view k_category = "math";

/// True if `node` is a number literal whose value is exactly 2 (or 2.0, 2.0f).
[[nodiscard]] bool is_literal_two(::TSNode node, std::string_view bytes) noexcept {
    if (node_kind(node) != "number_literal")
        return false;
    const auto text = node_text(node, bytes);
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size() || text[i] != '2')
        return false;
    ++i;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        const char c = text[i];
        if (c != 'f' && c != 'F' && c != 'h' && c != 'H' && c != 'l' && c != 'L')
            return false;
        ++i;
    }
    return true;
}

/// True if `node` is a call to `dot` with exactly 2 arguments.
/// Returns the two argument nodes via out-params.
[[nodiscard]] bool is_dot_call(::TSNode node,
                               std::string_view bytes,
                               ::TSNode& arg0_out,
                               ::TSNode& arg1_out) noexcept {
    if (node_kind(node) != "call_expression")
        return false;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (node_text(fn, bytes) != "dot")
        return false;
    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 2U)
        return false;
    arg0_out = ::ts_node_named_child(args, 0);
    arg1_out = ::ts_node_named_child(args, 1);
    return !::ts_node_is_null(arg0_out) && !::ts_node_is_null(arg1_out);
}

/// Returns the anonymous (operator) text of a binary_expression.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        return node_text(child, bytes);
    }
    return {};
}

/// Try to match the right-hand side of `v - RHS` as `2 * dot(n,v) * n`
/// (or commutative variants). The standard HLSL formula is:
///
///   RHS = 2.0 * dot(n, v) * n
///       = (2.0 * dot(n, v)) * n   (left-associative)
///
/// tree-sitter parses `2.0 * dot(n,v) * n` as:
///   (binary_expression
///       left: (binary_expression
///           left: 2.0
///           right: dot(n,v))
///       right: n)
///
/// We also accept:
///   dot(n, v) * 2 * n   → same structure with 2 on right of inner mul
///   2 * n * dot(n, v)   (less common, also handled)
///
/// Returns (v_text, n_text) if matched, nullopt otherwise.
struct ReflectMatch {
    std::string v_text;
    std::string n_text;
};

[[nodiscard]] std::optional<ReflectMatch> try_match_reflect_rhs(::TSNode rhs,
                                                                ::TSNode v_node,
                                                                std::string_view bytes) noexcept {
    // RHS should be a binary_expression with operator `*`.
    if (node_kind(rhs) != "binary_expression")
        return std::nullopt;
    if (binary_op(rhs, bytes) != "*")
        return std::nullopt;

    const ::TSNode rhs_left = ::ts_node_child_by_field_name(rhs, "left", 4);
    const ::TSNode rhs_right = ::ts_node_child_by_field_name(rhs, "right", 5);
    if (::ts_node_is_null(rhs_left) || ::ts_node_is_null(rhs_right))
        return std::nullopt;

    const auto v_text = node_text(v_node, bytes);

    // Expect outermost: (inner_mul) * n
    // where inner_mul = 2 * dot(n, v)  or  dot(n, v) * 2
    // and the trailing `* n` node matches the `v` node in `dot(n, v)`.

    auto try_extract = [&](::TSNode inner, ::TSNode tail_n) -> std::optional<ReflectMatch> {
        if (node_kind(inner) != "binary_expression")
            return std::nullopt;
        if (binary_op(inner, bytes) != "*")
            return std::nullopt;

        const ::TSNode il = ::ts_node_child_by_field_name(inner, "left", 4);
        const ::TSNode ir = ::ts_node_child_by_field_name(inner, "right", 5);
        if (::ts_node_is_null(il) || ::ts_node_is_null(ir))
            return std::nullopt;

        // Find which operand is "2" and which is "dot(n, v)".
        ::TSNode dot_node{};
        if (is_literal_two(il, bytes)) {
            dot_node = ir;
        } else if (is_literal_two(ir, bytes)) {
            dot_node = il;
        } else {
            return std::nullopt;
        }

        ::TSNode da{}, db{};
        if (!is_dot_call(dot_node, bytes, da, db))
            return std::nullopt;

        // dot(n, v) — da and db must be {n_text, v_text} in some order.
        const auto ta = node_text(da, bytes);
        const auto tb = node_text(db, bytes);
        const auto tn = node_text(tail_n, bytes);

        // tail_n should be the normal vector (n).
        // The two dot args must be {n, v} in some order.
        // tail_n must equal one of them, and v must equal the other.
        if (tn.empty() || v_text.empty())
            return std::nullopt;

        std::string_view n_candidate;
        if (ta == v_text && tb == tn) {
            n_candidate = tb;
        } else if (tb == v_text && ta == tn) {
            n_candidate = ta;
        } else {
            return std::nullopt;
        }

        return ReflectMatch{std::string{v_text}, std::string{n_candidate}};
    };

    // Pattern: (2 * dot(n,v)) * n
    auto result = try_extract(rhs_left, rhs_right);
    if (result)
        return result;

    // Pattern: n * (2 * dot(n,v))  — less common but commutative
    result = try_extract(rhs_right, rhs_left);
    return result;
}

/// Walk every node in the tree looking for `v - RHS` where RHS matches the
/// reflect formula. Emit a suggestion diagnostic when found.
void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression" && binary_op(node, bytes) == "-") {
        const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        const ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);
        if (!::ts_node_is_null(lhs) && !::ts_node_is_null(rhs)) {
            const auto match = try_match_reflect_rhs(rhs, lhs, bytes);
            if (match.has_value()) {
                const auto expr_range = tree.byte_range(node);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
                diag.message = std::string{"`"} + match->v_text + " - 2 * dot(" + match->n_text +
                               ", " + match->v_text + ") * " + match->n_text +
                               "` is the reflect formula — use `reflect(" + match->v_text + ", " +
                               match->n_text + ")`";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{"replace with `reflect("} + match->v_text + ", " +
                                  match->n_text +
                                  ")` — verify that argument order matches your "
                                  "incident/normal convention before applying";
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
                edit.replacement =
                    std::string{"reflect("} + match->v_text + ", " + match->n_text + ")";
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
                // Don't descend into the matched node — prevents double-firing.
                return;
            }
        }
    }

    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualReflect : public Rule {
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
        const auto bytes = tree.source_bytes();
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_reflect() {
    return std::make_unique<ManualReflect>();
}

}  // namespace hlsl_clippy::rules
