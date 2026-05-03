// lerp-extremes
//
// Detects `lerp(a, b, 0)` and `lerp(a, b, 1)` where the interpolant is a
// compile-time constant 0 or 1. In those cases the lerp reduces to one of
// its endpoints: `lerp(a, b, 0) == a` and `lerp(a, b, 1) == b`. Emitting
// a whole `lerp` call wastes an ALU instruction on every shader invocation.
//
// The match is purely syntactic: the third argument must be a numeric
// literal whose value rounds to exactly 0 or 1. The fix is machine-applicable.

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

constexpr std::string_view k_rule_id = "lerp-extremes";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_lerp_name = "lerp";

// Match any 3-arg call expression named "lerp" where the third arg is a
// number literal (host-side predicate will check 0 or 1).
constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (identifier) @fn
        arguments: (argument_list
            (_) @a
            (_) @b
            (number_literal) @t)) @call
)";

/// Returns true if `text` represents a numeric literal whose value is exactly
/// 0.0 or 1.0 (i.e., integer 0/1, or 0.0/1.0 with any float suffix).
[[nodiscard]] bool is_float_suffix_char(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

[[nodiscard]] bool literal_is(std::string_view text, double target) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (text[i] == '+')
        ++i;
    if (i >= text.size())
        return false;

    // Read integer part
    const std::size_t int_start = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        ++i;
    if (i == int_start)
        return false;
    const auto int_text = text.substr(int_start, i - int_start);

    // Classify integer part as Zero or One
    std::size_t k = 0;
    while (k < int_text.size() && int_text[k] == '0')
        ++k;
    const auto trimmed = int_text.substr(k);
    const bool is_zero = trimmed.empty();
    const bool is_one = (trimmed == "1");
    if (!is_zero && !is_one)
        return false;

    // Optional fractional part — must be all zeros
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (text[i] != '0')
                return false;
            ++i;
        }
    }

    // No exponent accepted (rare in shader code; keep rule conservative)
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;

    // Optional float suffix
    while (i < text.size()) {
        if (!is_float_suffix_char(text[i]))
            return false;
        ++i;
    }

    return (target == 0.0) ? is_zero : is_one;
}

class LerpExtremes : public Rule {
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
            diag.message = std::string{"failed to compile lerp-extremes query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(compiled.value(),
                   ::ts_tree_root_node(tree.raw_tree()),
                   [&](const query::QueryMatch& match) {
                       const ::TSNode fn = match.capture("fn");
                       const ::TSNode a = match.capture("a");
                       const ::TSNode b = match.capture("b");
                       const ::TSNode t = match.capture("t");
                       const ::TSNode call = match.capture("call");
                       if (::ts_node_is_null(fn) || ::ts_node_is_null(a) || ::ts_node_is_null(b) ||
                           ::ts_node_is_null(t) || ::ts_node_is_null(call)) {
                           return;
                       }
                       if (tree.text(fn) != k_lerp_name)
                           return;

                       const auto t_text = tree.text(t);
                       const bool is_zero = literal_is(t_text, 0.0);
                       const bool is_one = literal_is(t_text, 1.0);
                       if (!is_zero && !is_one)
                           return;

                       const auto call_range = tree.byte_range(call);
                       const auto result_node = is_zero ? a : b;
                       const auto result_text = tree.text(result_node);

                       Diagnostic diag;
                       diag.code = std::string{k_rule_id};
                       diag.severity = Severity::Warning;
                       diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};

                       if (is_zero) {
                           diag.message = std::string{
                               "`lerp(a, b, 0)` always returns `a` — the interpolant "
                               "is 0, so the result is the first endpoint"};
                       } else {
                           diag.message = std::string{
                               "`lerp(a, b, 1)` always returns `b` — the interpolant "
                               "is 1, so the result is the second endpoint"};
                       }

                       Fix fix;
                       fix.machine_applicable = true;
                       fix.description = is_zero ? std::string{"replace `lerp(a, b, 0)` with `a`"}
                                                 : std::string{"replace `lerp(a, b, 1)` with `b`"};
                       TextEdit edit;
                       edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                       edit.replacement = std::string{result_text};
                       fix.edits.push_back(std::move(edit));
                       diag.fixes.push_back(std::move(fix));

                       ctx.emit(std::move(diag));
                   });
    }
};

}  // namespace

std::unique_ptr<Rule> make_lerp_extremes() {
    return std::make_unique<LerpExtremes>();
}

}  // namespace shader_clippy::rules
