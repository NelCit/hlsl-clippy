// mul-identity
//
// Detects redundant arithmetic identity operations on scalars and vectors:
//   - `x * 1`  / `1 * x`  →  `x`   (machine-applicable)
//   - `x + 0`  / `0 + x`  →  `x`   (machine-applicable)
//   - `x * 0`  / `0 * x`  →  `0`   (suggestion-only — `x * 0` preserves NaN
//                                    for float; without type info we cannot
//                                    tell whether the result type is scalar or
//                                    vector, so we emit a suggestion instead of
//                                    a machine-applicable fix.)
//
// Only numeric literals are matched on the identity operand side — named
// constants, cbuffer uniforms, or runtime values that happen to be 0/1 are
// out of scope.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "query/query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "mul-identity";
constexpr std::string_view k_category = "math";

// Match any binary expression: captures both operands and the operator.
constexpr std::string_view k_pattern = R"(
    (binary_expression
        left:  (_) @left
        right: (_) @right) @expr
)";

[[nodiscard]] bool is_float_suffix_char(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

/// Returns true if `text` is a numeric literal whose value is exactly 0.
[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size())
        return false;
    // All digits must be '0'
    if (!(text[i] >= '0' && text[i] <= '9'))
        return false;
    while (i < text.size() && text[i] == '0')
        ++i;
    // Optional fractional part of all zeros
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    // No exponent
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    // Suffix
    while (i < text.size()) {
        if (!is_float_suffix_char(text[i]))
            return false;
        ++i;
    }
    return true;
}

/// Returns true if `text` is a numeric literal whose value is exactly 1.
[[nodiscard]] bool literal_is_one(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size())
        return false;
    // Skip leading zeros then exactly one '1'
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i >= text.size() || text[i] != '1')
        return false;
    ++i;
    // After the '1' must come only '0' digits / dot / suffix
    if (i < text.size() && text[i] >= '2' && text[i] <= '9')
        return false;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_float_suffix_char(text[i]))
            return false;
        ++i;
    }
    return true;
}

/// True if a TSNode is a number_literal.
[[nodiscard]] bool is_number_literal(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const char* t = ::ts_node_type(node);
    return t != nullptr && std::string_view{t} == "number_literal";
}

/// Return the operator text of a binary_expression node.
/// tree-sitter-hlsl represents the operator as an anonymous child (not a named
/// field), sitting between the left and right named children.
[[nodiscard]] std::string_view binary_operator(::TSNode expr, std::string_view source) noexcept {
    // Walk all children and find the anonymous node that is the operator.
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child))
            continue;
        if (!::ts_node_is_named(child)) {
            // Anonymous nodes in a binary_expression are the operators.
            const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
            const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
            if (lo < source.size() && hi <= source.size() && hi > lo) {
                return source.substr(lo, hi - lo);
            }
        }
    }
    return {};
}

class MulIdentity : public Rule {
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
        auto compiled = query::Query::compile(tree.language(), k_pattern);
        if (!compiled.has_value()) {
            Diagnostic diag;
            diag.code = std::string{"clippy::query-compile"};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            diag.message = std::string{"failed to compile mul-identity query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(compiled.value(),
                   ::ts_tree_root_node(tree.raw_tree()),
                   [&](const query::QueryMatch& match) {
                       const ::TSNode left = match.capture("left");
                       const ::TSNode right = match.capture("right");
                       const ::TSNode expr = match.capture("expr");
                       if (::ts_node_is_null(left) || ::ts_node_is_null(right) ||
                           ::ts_node_is_null(expr)) {
                           return;
                       }

                       const auto op = binary_operator(expr, tree.source_bytes());
                       if (op.empty())
                           return;

                       const auto left_text = tree.text(left);
                       const auto right_text = tree.text(right);
                       const auto expr_range = tree.byte_range(expr);

                       // ---- x * 1  or  1 * x ----
                       if (op == "*") {
                           // right is 1
                           if (is_number_literal(right) && literal_is_one(right_text)) {
                               emit_identity(tree,
                                             ctx,
                                             expr_range,
                                             left_text,
                                             "multiply by 1 is a no-op",
                                             "replace `x * 1` with `x`",
                                             true);
                               return;
                           }
                           // left is 1
                           if (is_number_literal(left) && literal_is_one(left_text)) {
                               emit_identity(tree,
                                             ctx,
                                             expr_range,
                                             right_text,
                                             "multiply by 1 is a no-op",
                                             "replace `1 * x` with `x`",
                                             true);
                               return;
                           }
                           // right is 0 (suggestion-only)
                           if (is_number_literal(right) && literal_is_zero(right_text)) {
                               emit_mul_zero(tree,
                                             ctx,
                                             expr_range,
                                             right_text,
                                             "multiply by 0 is 0 for finite values, but "
                                             "preserves NaN for floats — use `0` directly "
                                             "only if the operand cannot be NaN");
                               return;
                           }
                           // left is 0 (suggestion-only)
                           if (is_number_literal(left) && literal_is_zero(left_text)) {
                               emit_mul_zero(tree,
                                             ctx,
                                             expr_range,
                                             left_text,
                                             "multiply by 0 is 0 for finite values, but "
                                             "preserves NaN for floats — use `0` directly "
                                             "only if the operand cannot be NaN");
                               return;
                           }
                       }

                       // ---- x + 0  or  0 + x ----
                       if (op == "+") {
                           if (is_number_literal(right) && literal_is_zero(right_text)) {
                               emit_identity(tree,
                                             ctx,
                                             expr_range,
                                             left_text,
                                             "adding 0 is a no-op",
                                             "replace `x + 0` with `x`",
                                             true);
                               return;
                           }
                           if (is_number_literal(left) && literal_is_zero(left_text)) {
                               emit_identity(tree,
                                             ctx,
                                             expr_range,
                                             right_text,
                                             "adding 0 is a no-op",
                                             "replace `0 + x` with `x`",
                                             true);
                               return;
                           }
                       }
                   });
    }

private:
    static void emit_identity(const AstTree& tree,
                              RuleContext& ctx,
                              ByteSpan expr_range,
                              std::string_view result_text,
                              std::string_view message_fragment,
                              std::string_view fix_description,
                              bool machine_applicable) {
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
        diag.message = std::string{message_fragment};

        Fix fix;
        fix.machine_applicable = machine_applicable;
        fix.description = std::string{fix_description};
        TextEdit edit;
        edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
        edit.replacement = std::string{result_text};
        fix.edits.push_back(std::move(edit));
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }

    static void emit_mul_zero(const AstTree& tree,
                              RuleContext& ctx,
                              ByteSpan expr_range,
                              std::string_view zero_text,
                              std::string_view message_fragment) {
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
        diag.message = std::string{message_fragment};

        Fix fix;
        fix.machine_applicable = false;
        fix.description = std::string{
            "if the operand is guaranteed finite (non-NaN), replace the "
            "multiply-by-zero with the literal `0` (or `0.0`) directly; "
            "otherwise leave as-is to preserve NaN propagation"};
        TextEdit edit;
        edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
        edit.replacement = std::string{zero_text};
        fix.edits.push_back(std::move(edit));
        diag.fixes.push_back(std::move(fix));

        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_mul_identity() {
    return std::make_unique<MulIdentity>();
}

}  // namespace hlsl_clippy::rules
