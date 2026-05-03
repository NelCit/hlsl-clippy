// countbits-vs-manual-popcount
//
// Detects the textbook hand-rolled population-count loop:
//
//     for (int i = 0; i < 32; ++i) {
//         count += (x & 1);
//         x >>= 1;
//     }
//
// HLSL has a dedicated `countbits(uint)` intrinsic that lowers to a single
// hardware popcount instruction on every modern GPU (NV, AMD, Intel, Mali,
// Adreno). Open-coded loops cost up to 32 ALU instructions for the same
// result.
//
// Detection heuristic: a `for` or `while` statement whose body contains
//   - a compound-assignment with operator `+=` whose right-hand side is a
//     `binary_expression` with operator `&` and a literal `1` operand
//     (i.e. `count += (x & 1)` or `count += x & 1`), AND
//   - a shift operation somewhere in the body or the loop's update clause
//     (`x >>= 1`, `x = x >> 1`, or a binary `>>` expression)
//
// The fix is SUGGESTION-ONLY: replacing the loop with `countbits(x)` requires
// knowing the variable being shifted is the input, that the counter is fresh,
// and that no other side-effects happen in the loop body. We surface the
// suggestion and let the developer confirm.

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

constexpr std::string_view k_rule_id = "countbits-vs-manual-popcount";
constexpr std::string_view k_category = "math";

/// Find the operator (anonymous child) text for any node that exposes one
/// (binary_expression, assignment_expression, update_expression, etc.).
[[nodiscard]] std::string_view operator_text(::TSNode node, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(node, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        const auto t = node_text(child, bytes);
        if (!t.empty())
            return t;
    }
    return {};
}

/// True if the node is a number_literal whose textual value is exactly `1`
/// (allowing an optional unsigned suffix `u` or `U`).
[[nodiscard]] bool is_literal_one(::TSNode node, std::string_view bytes) noexcept {
    if (node_kind(node) != "number_literal")
        return false;
    const auto text = node_text(node, bytes);
    if (text.empty() || text[0] != '1')
        return false;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L')
            return false;
    }
    return true;
}

/// True if `node` is a `binary_expression` of the form `<expr> & 1` or
/// `1 & <expr>`. This unwraps any number of layers of `parenthesized_expression`.
[[nodiscard]] bool is_mask_low_bit(::TSNode node, std::string_view bytes) noexcept {
    while (node_kind(node) == "parenthesized_expression") {
        // The inner expression is the (sole) named child.
        const uint32_t nc = ::ts_node_named_child_count(node);
        if (nc < 1U)
            return false;
        node = ::ts_node_named_child(node, 0);
    }
    if (node_kind(node) != "binary_expression")
        return false;
    if (operator_text(node, bytes) != "&")
        return false;
    const ::TSNode l = ::ts_node_child_by_field_name(node, "left", 4);
    const ::TSNode r = ::ts_node_child_by_field_name(node, "right", 5);
    return is_literal_one(l, bytes) || is_literal_one(r, bytes);
}

/// True if `node`'s subtree contains, anywhere, a `+= <expr & 1>` style
/// assignment. We accept either an assignment_expression or update_expression
/// node whose operator text is `+=`, with the RHS being a `& 1` binary expr.
[[nodiscard]] bool has_popcount_increment(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const auto kind = node_kind(node);
    if (kind == "assignment_expression" || kind == "update_expression" ||
        kind == "augmented_assignment_expression") {
        if (operator_text(node, bytes) == "+=") {
            // Try common field names for the RHS.
            ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);
            if (::ts_node_is_null(rhs)) {
                rhs = ::ts_node_child_by_field_name(node, "value", 5);
            }
            if (::ts_node_is_null(rhs) && ::ts_node_named_child_count(node) >= 2U) {
                rhs = ::ts_node_named_child(node, 1);
            }
            if (!::ts_node_is_null(rhs) && is_mask_low_bit(rhs, bytes)) {
                return true;
            }
        }
    }
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        if (has_popcount_increment(::ts_node_child(node, i), bytes))
            return true;
    }
    return false;
}

/// True if `node`'s subtree contains a right-shift somewhere: either a `>>=`
/// assignment, a `>>` binary expression, or `x = x >> N`. Exists to
/// disambiguate the popcount loop from a generic accumulator.
[[nodiscard]] bool has_right_shift(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const auto kind = node_kind(node);
    if (kind == "binary_expression" && operator_text(node, bytes) == ">>")
        return true;
    if ((kind == "assignment_expression" || kind == "update_expression" ||
         kind == "augmented_assignment_expression") &&
        operator_text(node, bytes) == ">>=") {
        return true;
    }
    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        if (has_right_shift(::ts_node_child(node, i), bytes))
            return true;
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    const auto kind = node_kind(node);
    if (kind == "for_statement" || kind == "while_statement") {
        if (has_popcount_increment(node, bytes) && has_right_shift(node, bytes)) {
            const auto loop_range = tree.byte_range(node);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = loop_range};
            diag.message = std::string{
                "this loop looks like an open-coded population count "
                "(`+= (x & 1)` plus a right shift) -- use the "
                "`countbits(x)` intrinsic, which lowers to a single hardware "
                "popcount instruction"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "replace the loop body with `count = countbits(x);` (or fold the "
                "result directly into the accumulator). Verify that the loop has no "
                "side-effects beyond counting and shifting before applying"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
            // Don't descend further into this loop -- prevents nested loops
            // inside the same body from double-firing on the same evidence.
            return;
        }
    }

    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class CountBitsVsManualPopcount : public Rule {
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

std::unique_ptr<Rule> make_countbits_vs_manual_popcount() {
    return std::make_unique<CountBitsVsManualPopcount>();
}

}  // namespace shader_clippy::rules
