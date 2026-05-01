// comparison-with-nan-literal
//
// Flags any comparison whose other operand textually evaluates to NaN. Every
// comparison involving NaN — including `NaN == NaN` — evaluates to false in
// IEEE 754, so the test is always-false (or always-true for `!=`) and almost
// certainly a bug. The intended idiom is `isnan(x)`.
//
// Detection is purely textual: the rule walks every binary_expression with a
// comparison operator (`==`, `!=`, `<`, `<=`, `>`, `>=`) and checks whether
// either operand's source text matches one of the canonical NaN forms:
//
//   - `NAN`, `nan`        — common macro / constant names
//   - `0.0/0.0`            — IEEE-754 NaN-producing division
//   - `(0.0/0.0)`          — same, parenthesised
//   - `0.0f/0.0f`          — float-suffix variant
//   - `(0.0f/0.0f)`        — same, parenthesised
//   - `1.#QNAN`, `1.#IND`  — MSVC printf-style NaN/indefinite tokens
//   - `-1.#IND`            — MSVC indefinite (negated)
//
// The fix is suggestion-only: rewriting `x == NaN` to `isnan(x)` is correct
// in spirit, but `x != NaN` semantics ("is not NaN") and `x < NaN` semantics
// ("never true") differ, so the right replacement depends on user intent.

#include <algorithm>
#include <array>
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

constexpr std::string_view k_rule_id = "comparison-with-nan-literal";
constexpr std::string_view k_category = "math";

// Match every binary_expression. We filter on operator and operand-text host-side.
constexpr std::string_view k_pattern = R"(
    (binary_expression
        left:  (_) @left
        right: (_) @right) @expr
)";

[[nodiscard]] std::string_view binary_op(::TSNode expr,
                                         std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child)) continue;
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
        if (lo < bytes.size() && hi <= bytes.size() && hi > lo) {
            return bytes.substr(lo, hi - lo);
        }
    }
    return {};
}

[[nodiscard]] bool is_comparison_op(std::string_view op) noexcept {
    return op == "==" || op == "!=" || op == "<" || op == "<=" ||
           op == ">"  || op == ">=";
}

/// Strip ASCII whitespace from both ends of `s`.
[[nodiscard]] std::string_view trim_ws(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                           s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                           s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Strip *all* whitespace from `s` for "shape" comparisons against NaN forms
/// like `0.0 / 0.0` versus `0.0/0.0`.
[[nodiscard]] std::string strip_all_ws(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            out += c;
        }
    }
    return out;
}

/// True if `text` (already whitespace-stripped) is one of the canonical NaN
/// literal forms recognised by this rule.
[[nodiscard]] bool is_nan_literal_text(std::string_view text) noexcept {
    static constexpr std::array<std::string_view, 10> k_forms = {
        "NAN",
        "nan",
        "0.0/0.0",
        "(0.0/0.0)",
        "0.0f/0.0f",
        "(0.0f/0.0f)",
        "1.#QNAN",
        "1.#IND",
        "-1.#IND",
        "(-1.#IND)",
    };
    for (auto form : k_forms) {
        if (text == form) return true;
    }
    return false;
}

class ComparisonWithNanLiteral : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return k_rule_id; }
    [[nodiscard]] std::string_view category() const noexcept override { return k_category; }
    [[nodiscard]] Stage stage() const noexcept override { return Stage::Ast; }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        auto compiled = query::Query::compile(tree.language(), k_pattern);
        if (!compiled.has_value()) {
            Diagnostic diag;
            diag.code     = std::string{"clippy::query-compile"};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            diag.message =
                std::string{"failed to compile comparison-with-nan-literal query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(
            compiled.value(),
            ::ts_tree_root_node(tree.raw_tree()),
            [&](const query::QueryMatch& match) {
                const ::TSNode left  = match.capture("left");
                const ::TSNode right = match.capture("right");
                const ::TSNode expr  = match.capture("expr");
                if (::ts_node_is_null(left) || ::ts_node_is_null(right) ||
                    ::ts_node_is_null(expr)) {
                    return;
                }

                const auto op = binary_op(expr, tree.source_bytes());
                if (!is_comparison_op(op)) return;

                const auto left_text  = trim_ws(tree.text(left));
                const auto right_text = trim_ws(tree.text(right));
                const auto left_normalised  = strip_all_ws(left_text);
                const auto right_normalised = strip_all_ws(right_text);

                std::string_view nan_side;
                std::string_view other_side_text;
                if (is_nan_literal_text(left_normalised)) {
                    nan_side = left_text;
                    other_side_text = right_text;
                } else if (is_nan_literal_text(right_normalised)) {
                    nan_side = right_text;
                    other_side_text = left_text;
                } else {
                    return;
                }

                const auto expr_range = tree.byte_range(expr);

                Diagnostic diag;
                diag.code     = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = expr_range};
                diag.message =
                    std::string{"comparison with NaN literal `"} +
                    std::string{nan_side} +
                    "` is always false (or always true for `!=`) per IEEE-754 — "
                    "use `isnan(" + std::string{other_side_text} + ")` instead";

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "replace the comparison with `isnan(x)` (or `!isnan(x)` for "
                    "the negated form); the textual rewrite depends on whether "
                    "the original `op` was `==`, `!=`, or an ordering `<`/`>`"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_comparison_with_nan_literal() {
    return std::make_unique<ComparisonWithNanLiteral>();
}

}  // namespace hlsl_clippy::rules
