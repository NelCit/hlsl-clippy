// compare-equal-float
//
// Flags `==` and `!=` comparisons whose operands look like floating-point
// values. Exact equality on float is unreliable: rounding error from any
// upstream arithmetic, denormal flushing on some GPUs, and NaN propagation
// (NaN is never equal to itself, so `x == x` is false for NaN inputs) all
// conspire to make textual `==` a frequent source of subtle bugs.
//
// Conservative match: a binary_expression with operator `==` or `!=` where
// at least one operand is a `number_literal` whose text contains a `.` or a
// trailing `f`/`F` suffix. Integer-only comparisons (`a == 0`, `idx != 4`)
// are intentionally NOT matched — they are usually fine on integer types and
// would generate too much noise.
//
// The fix is suggestion-only: rewriting `a == b` to `abs(a - b) < epsilon`
// requires picking an epsilon, which depends on the dynamic range of the
// values and is not safe to choose mechanically.

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

constexpr std::string_view k_rule_id = "compare-equal-float";
constexpr std::string_view k_category = "math";

// Match any binary expression; we filter operator and operand kinds host-side.
constexpr std::string_view k_pattern = R"(
    (binary_expression
        left:  (_) @left
        right: (_) @right) @expr
)";

/// Return the operator text of a binary_expression. tree-sitter-hlsl stores
/// the operator as an anonymous child sitting between the named left/right.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
        if (lo < bytes.size() && hi <= bytes.size() && hi > lo) {
            return bytes.substr(lo, hi - lo);
        }
    }
    return {};
}

/// True if `node` is a `number_literal`.
[[nodiscard]] bool is_number_literal(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return false;
    const char* t = ::ts_node_type(node);
    return t != nullptr && std::string_view{t} == "number_literal";
}

/// True if a numeric-literal text looks like a floating-point literal: it
/// contains a `.` or ends with an `f`/`F` suffix. The decimal-only form
/// `42` (an integer literal) is intentionally rejected.
[[nodiscard]] bool literal_looks_like_float(std::string_view text) noexcept {
    if (text.empty())
        return false;
    for (char c : text) {
        if (c == '.')
            return true;
    }
    const char last = text.back();
    if (last == 'f' || last == 'F')
        return true;
    // `1e3` style without a `.` is also a float literal in C/HLSL.
    for (char c : text) {
        if (c == 'e' || c == 'E')
            return true;
    }
    return false;
}

class CompareEqualFloat : public Rule {
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
            diag.message = std::string{"failed to compile compare-equal-float query"};
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

                       const auto op = binary_op(expr, tree.source_bytes());
                       if (op != "==" && op != "!=")
                           return;

                       const auto left_text = tree.text(left);
                       const auto right_text = tree.text(right);

                       const bool left_is_float_lit =
                           is_number_literal(left) && literal_looks_like_float(left_text);
                       const bool right_is_float_lit =
                           is_number_literal(right) && literal_looks_like_float(right_text);

                       if (!left_is_float_lit && !right_is_float_lit)
                           return;

                       const auto expr_range = tree.byte_range(expr);

                       Diagnostic diag;
                       diag.code = std::string{k_rule_id};
                       diag.severity = Severity::Warning;
                       diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
                       diag.message = std::string{"exact `"} + std::string{op} +
                                      "` on floating-point values is unreliable — rounding error "
                                      "and NaN make the result unpredictable; use an explicit "
                                      "tolerance such as `abs(a - b) < epsilon` instead";

                       Fix fix;
                       fix.machine_applicable = false;
                       fix.description = std::string{
                           "replace exact float equality with a tolerance check, "
                           "e.g. `abs(a - b) < epsilon` (or `>= epsilon` for `!=`); "
                           "choose epsilon based on the expected dynamic range"};
                       diag.fixes.push_back(std::move(fix));

                       ctx.emit(std::move(diag));
                   });
    }
};

}  // namespace

std::unique_ptr<Rule> make_compare_equal_float() {
    return std::make_unique<CompareEqualFloat>();
}

}  // namespace hlsl_clippy::rules
