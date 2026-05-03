// redundant-precision-cast
//
// Flags an outer cast whose target type matches the inner cast's target type:
//
//     (float)((float)x)        →  (float)x
//     (int)((int)x)            →  (int)x
//     (uint)( (uint)y )        →  (uint)y
//     (half)((half)z)          →  (half)z
//
// The inner cast already produces a value of the requested type, so the outer
// cast is a no-op at the IR level — it survives only as a textual round-trip
// that obscures the author's intent. Dropping the outer cast preserves the
// observed type and value exactly, so the fix is machine-applicable.
//
// Conservative match scope: this rule fires only when the outer and inner
// target types are textually identical *after whitespace normalisation*. Any
// type mismatch (`(float)((int)x)`, `(int)((uint)y)`) is left for a future
// rule that can model precision and signedness changes properly.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "query/query.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {

namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "redundant-precision-cast";
constexpr std::string_view k_category = "math";

// Match every cast_expression. We inspect the inner value host-side because
// the inner expression may be wrapped in a parenthesized_expression and we
// want to peel a single layer of parens before checking for an inner cast.
constexpr std::string_view k_pattern = R"(
    (cast_expression
        type: (_) @outer_type
        value: (_) @outer_value) @outer_cast
)";

/// Normalise a type-descriptor's text by stripping all whitespace. This
/// handles trivia such as `(float )` or `( float)` cleanly.
[[nodiscard]] std::string normalise_type(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            out += c;
        }
    }
    return out;
}

/// Peel one optional layer of `parenthesized_expression` so we can detect
/// the common `(float)((float)x)` form where the inner cast is wrapped in
/// parens for grouping.
[[nodiscard]] ::TSNode unwrap_parens(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return node;
    if (node_kind(node) == "parenthesized_expression") {
        const auto nc = ::ts_node_named_child_count(node);
        if (nc == 1U) {
            return ::ts_node_named_child(node, 0);
        }
    }
    return node;
}

class RedundantPrecisionCast : public Rule {
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
            diag.message = std::string{"failed to compile redundant-precision-cast query"};
            ctx.emit(std::move(diag));
            return;
        }

        query::QueryEngine engine;
        engine.run(compiled.value(),
                   ::ts_tree_root_node(tree.raw_tree()),
                   [&](const query::QueryMatch& match) {
                       const ::TSNode outer_cast = match.capture("outer_cast");
                       const ::TSNode outer_type = match.capture("outer_type");
                       const ::TSNode outer_value = match.capture("outer_value");
                       if (::ts_node_is_null(outer_cast) || ::ts_node_is_null(outer_type) ||
                           ::ts_node_is_null(outer_value)) {
                           return;
                       }

                       // Peel one layer of parens around the value (the common shape
                       // `(float)((float)x)` wraps the inner cast in parens).
                       const ::TSNode inner = unwrap_parens(outer_value);
                       if (node_kind(inner) != "cast_expression")
                           return;

                       const ::TSNode inner_type = ::ts_node_child_by_field_name(inner, "type", 4);
                       if (::ts_node_is_null(inner_type))
                           return;

                       const auto outer_type_text = node_text(outer_type, tree.source_bytes());
                       const auto inner_type_text = node_text(inner_type, tree.source_bytes());
                       if (outer_type_text.empty() || inner_type_text.empty())
                           return;

                       if (normalise_type(outer_type_text) != normalise_type(inner_type_text)) {
                           return;
                       }

                       // Replace the outer cast with the inner cast verbatim — this
                       // drops the redundant outer wrapper while preserving the
                       // textually-meaningful inner cast.
                       const auto outer_range = tree.byte_range(outer_cast);
                       const auto inner_text = node_text(inner, tree.source_bytes());
                       if (inner_text.empty())
                           return;

                       Diagnostic diag;
                       diag.code = std::string{k_rule_id};
                       diag.severity = Severity::Warning;
                       diag.primary_span = Span{.source = tree.source_id(), .bytes = outer_range};
                       diag.message =
                           std::string{"redundant `("} + std::string{outer_type_text} +
                           ")` cast — the inner cast already produces a value of type `" +
                           std::string{inner_type_text} + "`; drop the outer cast";

                       Fix fix;
                       fix.machine_applicable = true;
                       fix.description = std::string{
                           "drop the redundant outer cast; the inner cast already "
                           "produces the requested type"};
                       TextEdit edit;
                       edit.span = Span{.source = tree.source_id(), .bytes = outer_range};
                       edit.replacement = std::string{inner_text};
                       fix.edits.push_back(std::move(edit));
                       diag.fixes.push_back(std::move(fix));

                       ctx.emit(std::move(diag));
                   });
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_precision_cast() {
    return std::make_unique<RedundantPrecisionCast>();
}

}  // namespace shader_clippy::rules
