// inv-sqrt-to-rsqrt
//
// Detects `1.0 / sqrt(x)` (or `1 / sqrt(x)`, `1.0f / sqrt(x)`) and suggests
// `rsqrt(x)`. Reciprocal square root is a single hardware instruction on every
// modern GPU; computing `sqrt` first and then dividing wastes a divide and
// produces strictly less accurate results on most architectures.
//
// The match is purely syntactic: a `binary_expression` with operator `/`
// whose left operand is the literal `1` (or any equivalent spelling) and
// whose right operand is a `call_expression` named `sqrt`.  The fix is
// machine-applicable.

#include <cstddef>
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

constexpr std::string_view k_rule_id = "inv-sqrt-to-rsqrt";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_sqrt_name = "sqrt";

// A binary_expression with a number_literal on the left and a call_expression
// on the right.  The host-side predicate verifies the operator is `/`, the
// literal is exactly 1, and the call's function is named `sqrt`.
constexpr std::string_view k_pattern = R"(
    (binary_expression
        left:  (number_literal) @one
        right: (call_expression
            function: (identifier) @fn
            arguments: (argument_list) @args) @inner_call) @expr
)";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

/// True if `text` is a numeric literal whose value is exactly 1.
[[nodiscard]] bool literal_is_one(std::string_view text) noexcept {
    if (text.empty()) return false;
    std::size_t i = 0;
    if (text[i] == '+') ++i;
    while (i < text.size() && text[i] == '0') ++i;
    if (i >= text.size() || text[i] != '1') return false;
    ++i;
    if (i < text.size() && text[i] >= '0' && text[i] <= '9') return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0') ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9') return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) return false;
    while (i < text.size()) {
        if (!is_float_suffix(text[i])) return false;
        ++i;
    }
    return true;
}

/// Return the operator text of a binary_expression node (anonymous child
/// between `left` and `right` named children).
[[nodiscard]] std::string_view binary_operator(::TSNode expr,
                                               std::string_view source) noexcept {
    const std::uint32_t count = ::ts_node_child_count(expr);
    for (std::uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child)) continue;
        const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(child));
        const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(child));
        if (lo < source.size() && hi <= source.size() && hi > lo) {
            return source.substr(lo, hi - lo);
        }
    }
    return {};
}

class InvSqrtToRsqrt : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return k_rule_id; }
    [[nodiscard]] std::string_view category() const noexcept override { return k_category; }
    [[nodiscard]] Stage stage() const noexcept override { return Stage::Ast; }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        auto compiled = query::Query::compile(tree.language(), k_pattern);
        if (!compiled.has_value()) {
            Diagnostic diag;
            diag.code = std::string{"clippy::query-compile"};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            diag.message = std::string{"failed to compile inv-sqrt-to-rsqrt query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(compiled.value(),
                   ::ts_tree_root_node(tree.raw_tree()),
                   [&](const query::QueryMatch& match) {
                       const ::TSNode one = match.capture("one");
                       const ::TSNode fn = match.capture("fn");
                       const ::TSNode args = match.capture("args");
                       const ::TSNode expr = match.capture("expr");
                       if (::ts_node_is_null(one) || ::ts_node_is_null(fn) ||
                           ::ts_node_is_null(args) || ::ts_node_is_null(expr)) {
                           return;
                       }

                       if (binary_operator(expr, tree.source_bytes()) != "/") return;
                       if (!literal_is_one(tree.text(one))) return;
                       if (tree.text(fn) != k_sqrt_name) return;

                       // sqrt should take exactly one argument.
                       if (::ts_node_named_child_count(args) != 1U) return;
                       const ::TSNode sqrt_arg = ::ts_node_named_child(args, 0);
                       if (::ts_node_is_null(sqrt_arg)) return;
                       const auto inner_text = tree.text(sqrt_arg);
                       if (inner_text.empty()) return;

                       const auto expr_range = tree.byte_range(expr);

                       Diagnostic diag;
                       diag.code = std::string{k_rule_id};
                       diag.severity = Severity::Warning;
                       diag.primary_span =
                           Span{.source = tree.source_id(), .bytes = expr_range};
                       diag.message = std::string{
                           "`1.0 / sqrt(x)` should be written as `rsqrt(x)` -- "
                           "`rsqrt` is a single hardware instruction on every "
                           "modern GPU"};

                       Fix fix;
                       fix.machine_applicable = true;
                       fix.description = std::string{
                           "replace `1.0 / sqrt(x)` with `rsqrt(x)`"};
                       TextEdit edit;
                       edit.span =
                           Span{.source = tree.source_id(), .bytes = expr_range};
                       std::string replacement;
                       replacement.reserve(inner_text.size() + 7);
                       replacement.append("rsqrt(");
                       replacement.append(inner_text);
                       replacement.append(")");
                       edit.replacement = std::move(replacement);
                       fix.edits.push_back(std::move(edit));
                       diag.fixes.push_back(std::move(fix));

                       ctx.emit(std::move(diag));
                   });
    }
};

}  // namespace

std::unique_ptr<Rule> make_inv_sqrt_to_rsqrt() {
    return std::make_unique<InvSqrtToRsqrt>();
}

}  // namespace hlsl_clippy::rules
