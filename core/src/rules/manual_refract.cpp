// manual-refract
//
// Detects hand-rolled implementation of `refract(I, N, eta)`. The closed-form
// HLSL implementation is:
//
//     float k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I));
//     if (k < 0.0)
//         return float3(0, 0, 0);
//     return eta * I - (eta * dot(N, I) + sqrt(k)) * N;
//
// Detection heuristic (AST-only, conservative): walk every function_definition
// and look for a return statement whose expression is structurally:
//
//     <scalar> * I - (<scalar> * dot(N, I) + sqrt(<expr>)) * N
//
// Specifically the return expression must be a top-level binary subtraction
// whose right-hand side is a multiplication of a parenthesised sum (containing
// a `sqrt(...)` call and a `dot(N, I)` call) with a vector identifier `N`.
// The k computation may be inlined into the sqrt argument or hoisted to a
// local; both shapes are matched because we look at the structural shape of
// the return expression itself.
//
// The fix is SUGGESTION-ONLY because pinning down which parameter plays the
// role of `I`, `N`, `eta` from the AST alone is fragile: programmer intent
// (e.g. inverted normal conventions, baked-in `eta`) may differ. We emit a
// Fix with a description but no edits.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

constexpr std::string_view k_rule_id = "manual-refract";
constexpr std::string_view k_category = "math";

/// Returns the anonymous (operator) text of a binary_expression.
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

/// If `node` is a parenthesized_expression with a single named child, return
/// that child; otherwise return `node` unchanged.
[[nodiscard]] ::TSNode unwrap_parens(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return node;
    if (node_kind(node) != "parenthesized_expression")
        return node;
    if (::ts_node_named_child_count(node) != 1U)
        return node;
    const ::TSNode inner = ::ts_node_named_child(node, 0);
    return ::ts_node_is_null(inner) ? node : inner;
}

/// True if `node` is a call to `name` (any arity).
[[nodiscard]] bool is_call_to(::TSNode node,
                              std::string_view bytes,
                              std::string_view name) noexcept {
    if (node_kind(node) != "call_expression")
        return false;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    return node_text(fn, bytes) == name;
}

/// True if `node` (or any descendant) is a `call_expression` whose function
/// name is `name`. Used as a permissive presence check.
[[nodiscard]] bool contains_call_to(::TSNode node,
                                    std::string_view bytes,
                                    std::string_view name) noexcept {
    if (::ts_node_is_null(node))
        return false;
    if (is_call_to(node, bytes, name))
        return true;
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (contains_call_to(::ts_node_child(node, i), bytes, name))
            return true;
    }
    return false;
}

/// True if `node` (or any descendant) is a `dot()` call whose two arguments
/// look like simple vector-typed identifiers (i.e. both args are `identifier`
/// nodes). This avoids matching `dot(some.field, vec)` accidentally and keeps
/// the heuristic conservative.
[[nodiscard]] bool contains_dot_of_two_identifiers(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return false;
    if (is_call_to(node, bytes, "dot")) {
        const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
        if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
            const ::TSNode a = ::ts_node_named_child(args, 0);
            const ::TSNode b = ::ts_node_named_child(args, 1);
            if (node_kind(a) == "identifier" && node_kind(b) == "identifier") {
                return true;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (contains_dot_of_two_identifiers(::ts_node_child(node, i), bytes))
            return true;
    }
    return false;
}

/// True if any direct or nested operand of a `*` binary_expression rooted at
/// `node` is an `identifier` node.
[[nodiscard]] bool mul_has_identifier_operand(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return false;
    if (node_kind(node) == "binary_expression" && binary_op(node, bytes) == "*") {
        const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        const ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);
        if (node_kind(unwrap_parens(lhs)) == "identifier")
            return true;
        if (node_kind(unwrap_parens(rhs)) == "identifier")
            return true;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (mul_has_identifier_operand(::ts_node_child(node, i), bytes))
            return true;
    }
    return false;
}

/// Try to match the return expression `eta * I - (eta * dot(N, I) + sqrt(...)) * N`.
/// We don't pin down which named identifier plays which role; we only check the
/// structural shape:
///   * top-level binary_expression with operator `-`
///   * RHS (after paren unwrap) is a binary_expression with operator `*`
///   * one operand of that `*` is a parenthesized sum (binary `+`) that
///     contains a `sqrt(...)` call AND a `dot(...)` call
///   * the other operand of that `*` is an identifier (the `N`)
///   * LHS contains an identifier somewhere as a `*` operand (the `eta * I`)
[[nodiscard]] bool return_expr_matches_refract(::TSNode expr, std::string_view bytes) noexcept {
    expr = unwrap_parens(expr);
    if (node_kind(expr) != "binary_expression")
        return false;
    if (binary_op(expr, bytes) != "-")
        return false;

    const ::TSNode lhs = ::ts_node_child_by_field_name(expr, "left", 4);
    const ::TSNode rhs_raw = ::ts_node_child_by_field_name(expr, "right", 5);
    if (::ts_node_is_null(lhs) || ::ts_node_is_null(rhs_raw))
        return false;

    const ::TSNode rhs = unwrap_parens(rhs_raw);
    if (node_kind(rhs) != "binary_expression")
        return false;
    if (binary_op(rhs, bytes) != "*")
        return false;

    const ::TSNode rhs_l = ::ts_node_child_by_field_name(rhs, "left", 4);
    const ::TSNode rhs_r = ::ts_node_child_by_field_name(rhs, "right", 5);
    if (::ts_node_is_null(rhs_l) || ::ts_node_is_null(rhs_r))
        return false;

    // One side must be a parenthesised sum containing sqrt() and dot();
    // the other side must be a (vector) identifier.
    auto looks_like_sum = [&](::TSNode candidate) {
        const ::TSNode inner = unwrap_parens(candidate);
        if (node_kind(inner) != "binary_expression")
            return false;
        if (binary_op(inner, bytes) != "+")
            return false;
        if (!contains_call_to(inner, bytes, "sqrt"))
            return false;
        if (!contains_call_to(inner, bytes, "dot"))
            return false;
        return true;
    };
    auto looks_like_n = [&](::TSNode candidate) {
        return node_kind(unwrap_parens(candidate)) == "identifier";
    };

    bool sum_l_n_r = looks_like_sum(rhs_l) && looks_like_n(rhs_r);
    bool sum_r_n_l = looks_like_sum(rhs_r) && looks_like_n(rhs_l);
    if (!sum_l_n_r && !sum_r_n_l)
        return false;

    // LHS must be a `*` whose operands include an identifier (the `eta * I`).
    if (!mul_has_identifier_operand(lhs, bytes))
        return false;

    // Sanity / belt-and-braces: the whole expression must mention dot(id, id).
    if (!contains_dot_of_two_identifiers(expr, bytes))
        return false;

    return true;
}

/// Walk every node in the tree looking for return statements whose expression
/// matches the manual-refract shape. Emit a suggestion diagnostic when found.
void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "return_statement") {
        // The returned expression is the first named child.
        if (::ts_node_named_child_count(node) >= 1U) {
            const ::TSNode expr = ::ts_node_named_child(node, 0);
            if (return_expr_matches_refract(expr, bytes)) {
                const auto stmt_range = tree.byte_range(node);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = stmt_range};
                diag.message = std::string{
                    "this return expression is the closed-form `refract(I, N, eta)` "
                    "formula -- prefer the built-in `refract()` intrinsic"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "replace the hand-rolled body with `return refract(I, N, eta);` "
                    "-- verify which parameter plays each role before applying"};
                // No edits: identifying I, N, eta from the AST is fragile.
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
                // Don't descend further into the matched return statement.
                return;
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualRefract : public Rule {
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

std::unique_ptr<Rule> make_manual_refract() {
    return std::make_unique<ManualRefract>();
}

}  // namespace shader_clippy::rules
