// redundant-transpose
//
// Detects the lexical `transpose(transpose(M))` pattern. `transpose` is its
// own inverse on any rectangular matrix: applying it twice yields the original
// matrix. Two HLSL transposes lower to a sequence of swizzle moves on every
// modern GPU, so the rewrite is a strict ALU win.
//
// The match is expressed declaratively via a TSQuery; the rule's predicate is
// just "both call function names are `transpose`". The fix is
// machine-applicable: replace the outer call with the inner call's text.

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

constexpr std::string_view k_rule_id = "redundant-transpose";
constexpr std::string_view k_category = "saturate-redundancy";
constexpr std::string_view k_transpose_name = "transpose";

constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (identifier) @outer
        arguments: (argument_list
            (call_expression
                function: (identifier) @inner) @inner_call)) @outer_call
)";

class RedundantTranspose : public Rule {
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
            diag.message = std::string{"failed to compile redundant-transpose query"};
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
                if (tree.text(outer) != k_transpose_name)
                    return;
                if (tree.text(inner) != k_transpose_name)
                    return;

                const auto outer_range = tree.byte_range(outer_call);
                const auto inner_range = tree.byte_range(inner_call);

                // We want to replace `transpose(transpose(M))` with `M`, not
                // with `transpose(M)`. Pull the sole argument out of the inner
                // call instead of using the inner call's full text.
                const ::TSNode inner_args =
                    ::ts_node_child_by_field_name(inner_call, "arguments", 9);
                if (::ts_node_is_null(inner_args))
                    return;
                if (::ts_node_named_child_count(inner_args) != 1U)
                    return;
                const ::TSNode m_node = ::ts_node_named_child(inner_args, 0);
                if (::ts_node_is_null(m_node))
                    return;

                const auto m_text = tree.text(m_node);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = outer_range};
                diag.message = std::string{
                    "`transpose(transpose(M))` is redundant -- transpose is its own "
                    "inverse; the two calls cancel"};

                if (!m_text.empty() && inner_range.hi <= outer_range.hi) {
                    Fix fix;
                    fix.machine_applicable = true;
                    fix.description =
                        std::string{"drop both `transpose` calls -- transpose is its own inverse"};
                    TextEdit edit;
                    edit.span = Span{.source = tree.source_id(), .bytes = outer_range};
                    edit.replacement = std::string{m_text};
                    fix.edits.push_back(std::move(edit));
                    diag.fixes.push_back(std::move(fix));
                }
                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_transpose() {
    return std::make_unique<RedundantTranspose>();
}

}  // namespace shader_clippy::rules
