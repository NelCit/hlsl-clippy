// redundant-saturate
//
// Detects the lexical `saturate(saturate(x))` pattern. `saturate` is
// idempotent: re-applying it generates a wasted ALU clamp on every modern GPU
// (the inner `saturate` already lowers to a free output modifier).
//
// The match is expressed declaratively via a TSQuery; the rule's predicate is
// just "both call function names are `saturate`". A machine-applicable fix
// will be wired in a follow-up commit.

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

constexpr std::string_view k_rule_id = "redundant-saturate";
constexpr std::string_view k_category = "saturate-redundancy";
constexpr std::string_view k_saturate_name = "saturate";

// The pattern matches a call expression whose argument list contains exactly
// one positional call expression as a sub-expression. The function-name
// equality check is performed as a host-side predicate.
constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (identifier) @outer
        arguments: (argument_list
            (call_expression
                function: (identifier) @inner) @inner_call)) @outer_call
)";

class RedundantSaturate : public Rule {
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
            // Compile errors are CI-time bugs; surface as a deny-level
            // diagnostic so they're impossible to ignore.
            Diagnostic diag;
            diag.code = std::string{"clippy::query-compile"};
            diag.severity = Severity::Error;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
            diag.message = std::string{"failed to compile redundant-saturate query"};
            ctx.emit(std::move(diag));
            return;
        }
        const auto& query = compiled.value();

        query::QueryEngine engine;
        engine.run(
            query, ::ts_tree_root_node(tree.raw_tree()), [&](const query::QueryMatch& match) {
                const ::TSNode outer = match.capture("outer");
                const ::TSNode inner = match.capture("inner");
                const ::TSNode outer_call = match.capture("outer_call");
                if (::ts_node_is_null(outer) || ::ts_node_is_null(inner) ||
                    ::ts_node_is_null(outer_call)) {
                    return;
                }
                if (tree.text(outer) != k_saturate_name) {
                    return;
                }
                if (tree.text(inner) != k_saturate_name) {
                    return;
                }

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(outer_call)};
                diag.message = std::string{
                    "`saturate(saturate(x))` is redundant — the inner "
                    "`saturate` already clamps to [0, 1]; drop the "
                    "outer call"};
                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_saturate() {
    return std::make_unique<RedundantSaturate>();
}

}  // namespace hlsl_clippy::rules
