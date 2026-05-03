// End-to-end tests for the static-sampler-when-dynamic-used rule.
// Reflection-stage suggestion: a sampler referenced by exactly one entry
// point is a candidate for promotion to a D3D12 static sampler.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_static_sampler_when_dynamic_used();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_static_sampler_when_dynamic_used());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("static-sampler-when-dynamic-used fires when sampler is used in one entry point",
          "[rules][static-sampler-when-dynamic-used]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "static-sampler-when-dynamic-used"));
}

TEST_CASE("static-sampler-when-dynamic-used does not fire when sampler is used in two entry points",
          "[rules][static-sampler-when-dynamic-used]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("vertex")]
float4 vs_main(uint vid : SV_VertexID) : SV_Position
{
    return base_color.SampleLevel(g_sampler, float2(0.0, 0.0), 0.0);
}

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "static-sampler-when-dynamic-used");
    }
}

TEST_CASE("static-sampler-when-dynamic-used does not fire on textures",
          "[rules][static-sampler-when-dynamic-used]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState linear_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(linear_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool any_on_texture = false;
    for (const auto& d : diags) {
        if (d.code == "static-sampler-when-dynamic-used" &&
            d.message.find("base_color") != std::string::npos) {
            any_on_texture = true;
        }
    }
    CHECK_FALSE(any_on_texture);
}

TEST_CASE("static-sampler-when-dynamic-used fires for SamplerComparisonState too",
          "[rules][static-sampler-when-dynamic-used]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

[shader("pixel")]
float ps_shadow(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleCmp(shadow_sampler, uv, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "static-sampler-when-dynamic-used"));
}

TEST_CASE("static-sampler-when-dynamic-used: reflection disabled means no fire",
          "[rules][static-sampler-when-dynamic-used]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_static_sampler_when_dynamic_used());
    LintOptions options;
    options.enable_reflection = false;
    const auto diags = lint(sources, src, rules, options);
    for (const auto& d : diags) {
        CHECK(d.code != "static-sampler-when-dynamic-used");
    }
}
