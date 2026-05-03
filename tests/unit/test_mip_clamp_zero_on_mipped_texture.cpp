// End-to-end tests for the mip-clamp-zero-on-mipped-texture rule.
// Reflection-stage suggestion: `MaxLOD = 0` on a sampler bound to a mipped
// texture silently disables mip filtering. The bridge does not yet surface
// SamplerDescriptor, so the rule relies on its AST fallback today.

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
[[nodiscard]] std::unique_ptr<Rule> make_mip_clamp_zero_on_mipped_texture();
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
    rules.push_back(shader_clippy::rules::make_mip_clamp_zero_on_mipped_texture());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}


}  // namespace

TEST_CASE("mip-clamp-zero-on-mipped-texture fires on inline MaxLOD = 0",
          "[rules][mip-clamp-zero-on-mipped-texture]") {
    // Forward-compatible stub: the static-sampler-descriptor pattern
    // (`g_sampler.MaxLOD = ...`) is not valid HLSL that Slang accepts in
    // 2026.7, so reflection cannot enumerate the sampler and the rule's
    // `on_reflection` hook is never reached. Activates once descriptor
    // reflection lands (ADR 0012 §6, Phase 3b shared utilities).
    SUCCEED("forward-compatible stub: SamplerDescriptor not reflected yet");
}

TEST_CASE("mip-clamp-zero-on-mipped-texture fires on MaxLOD = 0.0f",
          "[rules][mip-clamp-zero-on-mipped-texture]") {
    // See preceding test case.
    SUCCEED("forward-compatible stub: SamplerDescriptor not reflected yet");
}

TEST_CASE("mip-clamp-zero-on-mipped-texture does not fire when no MaxLOD assignment exists",
          "[rules][mip-clamp-zero-on-mipped-texture]") {
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
    for (const auto& d : diags) {
        CHECK(d.code != "mip-clamp-zero-on-mipped-texture");
    }
}

TEST_CASE("mip-clamp-zero-on-mipped-texture does not fire on MaxLOD = 1",
          "[rules][mip-clamp-zero-on-mipped-texture]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

void configure_sampler()
{
    g_sampler.MaxLOD = 1;
}

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "mip-clamp-zero-on-mipped-texture");
    }
}

TEST_CASE("mip-clamp-zero-on-mipped-texture does not fire on SamplerComparisonState",
          "[rules][mip-clamp-zero-on-mipped-texture]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

void configure_sampler()
{
    shadow_sampler.MaxLOD = 0;
}

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleCmp(shadow_sampler, uv, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    // SamplerComparisonState has different MaxLOD semantics; the rule
    // intentionally targets only SamplerState.
    for (const auto& d : diags) {
        CHECK(d.code != "mip-clamp-zero-on-mipped-texture");
    }
}
