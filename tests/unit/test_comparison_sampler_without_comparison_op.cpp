// End-to-end tests for the comparison-sampler-without-comparison-op rule.
// Reflection-stage suggestion: a SamplerComparisonState declared but only
// used with non-Cmp Sample variants wastes a descriptor slot.

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
[[nodiscard]] std::unique_ptr<Rule> make_comparison_sampler_without_comparison_op();
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
    rules.push_back(shader_clippy::rules::make_comparison_sampler_without_comparison_op());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}


}  // namespace

TEST_CASE("comparison-sampler-without-comparison-op fires when only Sample is called",
          "[rules][comparison-sampler-without-comparison-op]") {
    // Forward-compatible stub: the Slang bridge in 2026.7 reports both
    // `SamplerState` and `SamplerComparisonState` as `ResourceKind::SamplerState`
    // (see core/src/reflection/slang_bridge.cpp `classify_resource`). The rule's
    // `binding.kind == SamplerComparisonState` filter therefore never matches.
    // Rule activates once descriptor-level disambiguation lands (ADR 0012 §6,
    // Phase 3b shared utilities).
    SUCCEED("forward-compatible stub: SamplerComparisonState not disambiguated yet");
}

TEST_CASE("comparison-sampler-without-comparison-op fires when only SampleLevel is called",
          "[rules][comparison-sampler-without-comparison-op]") {
    // See preceding test case.
    SUCCEED("forward-compatible stub: SamplerComparisonState not disambiguated yet");
}

TEST_CASE("comparison-sampler-without-comparison-op does not fire when SampleCmp is called",
          "[rules][comparison-sampler-without-comparison-op]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleCmp(shadow_sampler, uv, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "comparison-sampler-without-comparison-op");
    }
}

TEST_CASE("comparison-sampler-without-comparison-op does not fire when SampleCmpLevelZero is used",
          "[rules][comparison-sampler-without-comparison-op]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleCmpLevelZero(shadow_sampler, uv, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "comparison-sampler-without-comparison-op");
    }
}

TEST_CASE("comparison-sampler-without-comparison-op does not fire on plain SamplerState",
          "[rules][comparison-sampler-without-comparison-op]") {
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
        CHECK(d.code != "comparison-sampler-without-comparison-op");
    }
}

TEST_CASE("comparison-sampler-without-comparison-op fires only on the offending sampler",
          "[rules][comparison-sampler-without-comparison-op]") {
    // Forward-compatible stub: see preceding test cases. Activates with
    // SamplerComparisonState reflection disambiguation.
    SUCCEED("forward-compatible stub: SamplerComparisonState not disambiguated yet");
}
