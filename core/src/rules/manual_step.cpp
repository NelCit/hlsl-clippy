// manual-step
//
// Detects the idiom `x > threshold ? 1.0 : 0.0` which is equivalent to
// `step(threshold, x)`. The GPU intrinsic is generally more efficient than a
// branch and makes the intent clear.
//
// Detection: a conditional_expression (ternary) whose:
//   - condition is a binary_expression with operator `>`
//   - consequence is a numeric literal 1 (or 1.0, 1.0f, …)
//   - alternative is a numeric literal 0 (or 0.0, 0.0f, …)
//
// The fix is SUGGESTION-ONLY because the `step` function uses `>=` semantics
// (step(a, x) = 1 if x >= a, 0 otherwise), while the hand-rolled form uses
// strict `>`. The boundary value differs for exact equality, so a human should
// verify whether the strict or non-strict comparison is intended.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "query/query.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "manual-step";
constexpr std::string_view k_category = "math";

// Match a ternary expression.  We'll verify the operator and literal values
// on the host side.
constexpr std::string_view k_pattern = R"(
    (conditional_expression
        condition: (_) @cond
        consequence: (number_literal) @then
        alternative: (number_literal) @else) @ternary
)";

[[nodiscard]] bool is_float_suffix_char(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size())
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

[[nodiscard]] bool literal_is_one(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i >= text.size() || text[i] != '1')
        return false;
    ++i;
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

/// Return the anonymous operator text within a binary_expression node.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
        if (lo < bytes.size() && hi <= bytes.size() && hi > lo)
            return bytes.substr(lo, hi - lo);
    }
    return {};
}

class ManualStep : public Rule {
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
            diag.message = std::string{"failed to compile manual-step query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(
            compiled.value(),
            ::ts_tree_root_node(tree.raw_tree()),
            [&](const query::QueryMatch& match) {
                const ::TSNode cond = match.capture("cond");
                const ::TSNode then_n = match.capture("then");
                const ::TSNode else_n = match.capture("else");
                const ::TSNode ternary = match.capture("ternary");
                if (::ts_node_is_null(cond) || ::ts_node_is_null(then_n) ||
                    ::ts_node_is_null(else_n) || ::ts_node_is_null(ternary)) {
                    return;
                }

                // Consequence must be 1, alternative must be 0.
                if (!literal_is_one(tree.text(then_n)))
                    return;
                if (!literal_is_zero(tree.text(else_n)))
                    return;

                // Condition must be a binary_expression with operator `>`.
                const char* cond_type = ::ts_node_type(cond);
                if (cond_type == nullptr || std::string_view{cond_type} != "binary_expression")
                    return;
                if (binary_op(cond, tree.source_bytes()) != ">")
                    return;

                // Extract x and threshold from the condition.
                const ::TSNode cond_left = ::ts_node_child_by_field_name(cond, "left", 4);
                const ::TSNode cond_right = ::ts_node_child_by_field_name(cond, "right", 5);
                if (::ts_node_is_null(cond_left) || ::ts_node_is_null(cond_right))
                    return;

                const auto x_text = tree.text(cond_left);
                const auto thr_text = tree.text(cond_right);
                const auto ternary_range = tree.byte_range(ternary);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = ternary_range};
                diag.message = std::string{"`"} + std::string{x_text} + " > " +
                               std::string{thr_text} +
                               " ? 1.0 : 0.0` is the `step` idiom — "
                               "note: `step(" +
                               std::string{thr_text} + ", " + std::string{x_text} +
                               ")` uses `>=` semantics; verify the boundary behaviour is correct";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{"replace with `step("} + std::string{thr_text} +
                                  ", " + std::string{x_text} +
                                  ")` — confirm whether `>` or `>=` at the "
                                  "threshold is intended (`step` uses `>=`)";
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = ternary_range};
                edit.replacement =
                    std::string{"step("} + std::string{thr_text} + ", " + std::string{x_text} + ")";
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_step() {
    return std::make_unique<ManualStep>();
}

}  // namespace shader_clippy::rules
