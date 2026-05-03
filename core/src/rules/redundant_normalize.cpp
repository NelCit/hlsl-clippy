// redundant-normalize
//
// Detects the lexical `normalize(normalize(x))` pattern. `normalize` is
// idempotent up to the precision of the underlying rsqrt: once a vector has
// unit length, normalising it again costs another rsqrt + multiply per
// component for no observable benefit. The rewrite drops the outer call.
//
// The match is expressed declaratively via a TSQuery; the rule's predicate is
// just "both call function names are `normalize`". The fix is
// machine-applicable: we replace the outer call's text with the inner call's
// text byte-for-byte, preserving any whitespace or comments inside the inner
// argument list.

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

constexpr std::string_view k_rule_id = "redundant-normalize";
constexpr std::string_view k_category = "saturate-redundancy";
constexpr std::string_view k_normalize_name = "normalize";

// Match a call expression whose sole argument is itself a call expression.
// The function-name equality check is performed as a host-side predicate.
constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (identifier) @outer
        arguments: (argument_list
            (call_expression
                function: (identifier) @inner) @inner_call)) @outer_call
)";

class RedundantNormalize : public Rule {
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
            diag.message = std::string{"failed to compile redundant-normalize query"};
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
                const ::TSNode inner_call = match.capture("inner_call");
                if (::ts_node_is_null(outer) || ::ts_node_is_null(inner) ||
                    ::ts_node_is_null(outer_call) || ::ts_node_is_null(inner_call)) {
                    return;
                }
                if (tree.text(outer) != k_normalize_name)
                    return;
                if (tree.text(inner) != k_normalize_name)
                    return;

                const auto outer_range = tree.byte_range(outer_call);
                const auto inner_range = tree.byte_range(inner_call);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = outer_range};
                diag.message = std::string{
                    "`normalize(normalize(x))` is redundant -- the inner "
                    "`normalize` already produces a unit-length vector; drop the "
                    "outer call"};

                const auto inner_text = tree.text(inner_call);
                if (!inner_text.empty() && inner_range.hi <= outer_range.hi) {
                    Fix fix;
                    fix.machine_applicable = true;
                    fix.description = std::string{
                        "drop the outer `normalize` (the inner call already produces a "
                        "unit-length vector)"};
                    TextEdit edit;
                    edit.span = Span{.source = tree.source_id(), .bytes = outer_range};
                    edit.replacement = std::string{inner_text};
                    fix.edits.push_back(std::move(edit));
                    diag.fixes.push_back(std::move(fix));
                }
                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_normalize() {
    return std::make_unique<RedundantNormalize>();
}

}  // namespace shader_clippy::rules
