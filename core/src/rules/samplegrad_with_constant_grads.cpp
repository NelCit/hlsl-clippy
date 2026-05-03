// samplegrad-with-constant-grads
//
// Detects `<tex>.SampleGrad(<sampler>, <uv>, <ddx>, <ddy>)` calls where the
// gradient arguments are both literal-zero vectors / scalars. With zero
// gradients the call is equivalent to `SampleLevel(<sampler>, <uv>, 0)` and
// avoids the explicit-gradient TMU instruction overhead.
//
// Detection plan: AST. Match `call_expression` with function shape
// `<id>.SampleGrad` and 4+ arguments. If the 3rd and 4th arguments are
// both number literals (or `float2(0,0)` / `float3(0,0,0)` calls) tokenising
// to zero, emit a suggestion.

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

constexpr std::string_view k_rule_id = "samplegrad-with-constant-grads";
constexpr std::string_view k_category = "texture";
// `tex` captures the field-expression LHS (the texture object); we need it to
// reconstruct the SampleLevel call when emitting the fix.
constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (field_expression
            argument: (_) @tex
            field: (field_identifier) @method)
        arguments: (argument_list
            (_) @sampler
            (_) @uv
            (_) @ddx
            (_) @ddy)) @call
)";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool token_is_zero_literal(std::string_view text) noexcept {
    text = trim(text);
    if (text.empty())
        return false;
    // Strip a leading sign.
    if (text[0] == '+' || text[0] == '-')
        text.remove_prefix(1);
    if (text.empty())
        return false;
    // Number literal: accept "0", "0.0", "0.0f", "0.0h" etc.
    if (text[0] >= '0' && text[0] <= '9') {
        std::size_t i = 0U;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (text[i] != '0')
                return false;
            ++i;
        }
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                if (text[i] != '0')
                    return false;
                ++i;
            }
        }
        while (i < text.size()) {
            const char c = text[i];
            if (c != 'f' && c != 'F' && c != 'h' && c != 'H' && c != 'l' && c != 'L')
                return false;
            ++i;
        }
        return true;
    }
    // Constructor: float2(0, 0), float3(0, 0, 0), float4(0,0,0,0).
    if (text.starts_with("float2(") || text.starts_with("float3(") || text.starts_with("float4(") ||
        text.starts_with("half2(") || text.starts_with("half3(") || text.starts_with("half4(")) {
        const auto lp = text.find('(');
        const auto rp = text.rfind(')');
        if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp + 1)
            return false;
        const auto inside = text.substr(lp + 1U, rp - lp - 1U);
        // All comma-separated tokens must be zero literals.
        std::size_t start = 0U;
        for (std::size_t i = 0U; i <= inside.size(); ++i) {
            if (i == inside.size() || inside[i] == ',') {
                const auto piece = trim(inside.substr(start, i - start));
                if (!token_is_zero_literal(piece))
                    return false;
                start = i + 1U;
            }
        }
        return true;
    }
    return false;
}

class SampleGradWithConstantGrads : public Rule {
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
        if (!compiled.has_value())
            return;
        query::QueryEngine engine;
        engine.run(
            compiled.value(),
            ::ts_tree_root_node(tree.raw_tree()),
            [&](const query::QueryMatch& match) {
                const ::TSNode tex = match.capture("tex");
                const ::TSNode method = match.capture("method");
                const ::TSNode sampler = match.capture("sampler");
                const ::TSNode uv = match.capture("uv");
                const ::TSNode ddx = match.capture("ddx");
                const ::TSNode ddy = match.capture("ddy");
                const ::TSNode call = match.capture("call");
                if (::ts_node_is_null(tex) || ::ts_node_is_null(method) ||
                    ::ts_node_is_null(sampler) || ::ts_node_is_null(uv) || ::ts_node_is_null(ddx) ||
                    ::ts_node_is_null(ddy) || ::ts_node_is_null(call))
                    return;
                if (tree.text(method) != "SampleGrad")
                    return;
                if (!token_is_zero_literal(tree.text(ddx)) ||
                    !token_is_zero_literal(tree.text(ddy)))
                    return;
                const auto range = tree.byte_range(call);
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = range};
                diag.message = std::string{
                    "`SampleGrad(s, uv, 0, 0)` is equivalent to `SampleLevel(s, uv, 0)` -- "
                    "the explicit-gradient TMU path costs extra issue cycles versus the "
                    "explicit-LOD path"};

                // Reconstruct the call as `<tex>.SampleLevel(<sampler>, <uv>, 0.0)`.
                // Texture/sampler/uv expressions in HLSL are conventionally
                // side-effect-free (resource references, varyings, member
                // accesses), so we mark the fix machine-applicable when all
                // three captures are simple identifiers or field accesses.
                const auto kind_simple = [](const std::string_view k) {
                    return k == "identifier" || k == "field_expression" ||
                           k == "subscript_expression";
                };
                const auto* tex_kind = ::ts_node_type(tex);
                const auto* sampler_kind = ::ts_node_type(sampler);
                const auto* uv_kind = ::ts_node_type(uv);
                const bool all_simple = tex_kind != nullptr && sampler_kind != nullptr &&
                                        uv_kind != nullptr && kind_simple(tex_kind) &&
                                        kind_simple(sampler_kind) && kind_simple(uv_kind);

                Fix fix;
                fix.machine_applicable = all_simple;
                fix.description =
                    all_simple ? std::string{"replace SampleGrad with SampleLevel"}
                               : std::string{
                                     "replace SampleGrad with SampleLevel; verify the texture, "
                                     "sampler, and UV expressions are side-effect-free first"};
                std::string replacement;
                const auto tex_text = tree.text(tex);
                const auto sampler_text = tree.text(sampler);
                const auto uv_text = tree.text(uv);
                replacement.reserve(tex_text.size() + sampler_text.size() + uv_text.size() + 24U);
                replacement.append(tex_text);
                replacement.append(".SampleLevel(");
                replacement.append(sampler_text);
                replacement.append(", ");
                replacement.append(uv_text);
                replacement.append(", 0.0)");
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = range};
                edit.replacement = std::move(replacement);
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_samplegrad_with_constant_grads() {
    return std::make_unique<SampleGradWithConstantGrads>();
}

}  // namespace shader_clippy::rules
