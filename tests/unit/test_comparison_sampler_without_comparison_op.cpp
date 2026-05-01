// End-to-end tests for the comparison-sampler-without-comparison-op rule.
// Reflection-stage suggestion: a SamplerComparisonState declared but only
// used with non-Cmp Sample variants wastes a descriptor slot.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_comparison_sampler_without_comparison_op();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_comparison_sampler_without_comparison_op());
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

TEST_CASE("comparison-sampler-without-comparison-op fires when only Sample is called",
          "[rules][comparison-sampler-without-comparison-op]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.Sample(shadow_sampler, uv).r;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "comparison-sampler-without-comparison-op"));
}

TEST_CASE("comparison-sampler-without-comparison-op fires when only SampleLevel is called",
          "[rules][comparison-sampler-without-comparison-op]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleLevel(shadow_sampler, uv, 0.0).r;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "comparison-sampler-without-comparison-op"));
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
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map_a;
Texture2D<float> shadow_map_b;
SamplerComparisonState ok_sampler;
SamplerComparisonState bad_sampler;

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    float a = shadow_map_a.SampleCmp(ok_sampler, uv, 0.5);
    float b = shadow_map_b.Sample(bad_sampler, uv).r;
    return a + b;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool fired_for_bad = false;
    bool fired_for_ok = false;
    for (const auto& d : diags) {
        if (d.code == "comparison-sampler-without-comparison-op") {
            if (d.message.find("bad_sampler") != std::string::npos) {
                fired_for_bad = true;
            }
            if (d.message.find("ok_sampler") != std::string::npos) {
                fired_for_ok = true;
            }
        }
    }
    CHECK(fired_for_bad);
    CHECK_FALSE(fired_for_ok);
}
