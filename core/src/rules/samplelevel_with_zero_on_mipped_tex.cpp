// samplelevel-with-zero-on-mipped-tex
//
// Detects `<tex>.SampleLevel(<sampler>, <uv>, 0)` calls where the LOD
// argument is a literal zero. On a mipped texture this defeats the texture
// cache's small-footprint fetch and disables trilinear blending entirely.
//
// Detection plan: AST-only. Walk every `call_expression` whose function shape
// is `<id>.SampleLevel`. If the third argument tokenises to a numeric literal
// whose value rounds to exactly 0, emit. We do not check whether the texture
// is mipped at the binding level -- that would require reflection
// `mip_count` which the bridge does not surface yet; a literal `0` LOD on
// any user-bound texture is highly suspicious by itself.

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

constexpr std::string_view k_rule_id = "samplelevel-with-zero-on-mipped-tex";
constexpr std::string_view k_category = "texture";
constexpr std::string_view k_pattern = R"(
    (call_expression
        function: (field_expression
            field: (field_identifier) @method)
        arguments: (argument_list
            (_) @sampler
            (_) @uv
            (number_literal) @lod)) @call
)";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    text = trim(text);
    if (text.empty())
        return false;
    if (text[0] == '+')
        text.remove_prefix(1);
    bool seen_digit = false;
    std::size_t i = 0U;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        if (text[i] != '0')
            return false;
        seen_digit = true;
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (text[i] != '0')
                return false;
            seen_digit = true;
            ++i;
        }
    }
    while (i < text.size()) {
        const char c = text[i];
        if (c != 'f' && c != 'F' && c != 'h' && c != 'H' && c != 'l' && c != 'L')
            return false;
        ++i;
    }
    return seen_digit;
}

class SampleLevelWithZero : public Rule {
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
                const ::TSNode method = match.capture("method");
                const ::TSNode lod = match.capture("lod");
                const ::TSNode call = match.capture("call");
                if (::ts_node_is_null(method) || ::ts_node_is_null(lod) || ::ts_node_is_null(call))
                    return;
                if (tree.text(method) != "SampleLevel")
                    return;
                if (!literal_is_zero(tree.text(lod)))
                    return;
                const auto range = tree.byte_range(call);
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = range};
                diag.message = std::string{
                    "`SampleLevel(s, uv, 0)` pins LOD to mip 0 -- on a mipped texture this "
                    "defeats the texture cache's small-footprint fetch and disables "
                    "trilinear filtering; replace with `Sample(s, uv)` in pixel-shader "
                    "context"};
                ctx.emit(std::move(diag));
            });
    }
};

}  // namespace

std::unique_ptr<Rule> make_samplelevel_with_zero_on_mipped_tex() {
    return std::make_unique<SampleLevelWithZero>();
}

}  // namespace hlsl_clippy::rules
