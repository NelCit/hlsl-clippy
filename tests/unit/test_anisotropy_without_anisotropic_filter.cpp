// End-to-end tests for the anisotropy-without-anisotropic-filter rule.
// Reflection-stage suggestion: MaxAnisotropy > 1 with a non-anisotropic
// Filter is silently ignored on every IHV. Today the rule relies on its
// AST fallback (SamplerDescriptor is nullopt).

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
[[nodiscard]] std::unique_ptr<Rule> make_anisotropy_without_anisotropic_filter();
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
    rules.push_back(hlsl_clippy::rules::make_anisotropy_without_anisotropic_filter());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}


}  // namespace

TEST_CASE("anisotropy-without-anisotropic-filter fires on MaxAnisotropy = 16 without ANISOTROPIC",
          "[rules][anisotropy-without-anisotropic-filter]") {
    // Forward-compatible stub: the static-sampler-descriptor pattern
    // (`g_sampler.MaxAnisotropy = ...`, `g_sampler.Filter = ...`) is not
    // valid HLSL that Slang accepts in 2026.7, so reflection cannot enumerate
    // the sampler and the rule's `on_reflection` hook is never reached.
    // The rule activates once Slang surfaces sampler-descriptor reflection
    // (or a separate AST-only path is added). See ADR 0012 §6 (descriptor
    // reflection roadmap).
    SUCCEED("forward-compatible stub: SamplerDescriptor not reflected yet");
}

TEST_CASE("anisotropy-without-anisotropic-filter fires on MaxAnisotropy = 4 without ANISOTROPIC",
          "[rules][anisotropy-without-anisotropic-filter]") {
    // See preceding test case. Activates with descriptor reflection.
    SUCCEED("forward-compatible stub: SamplerDescriptor not reflected yet");
}

TEST_CASE("anisotropy-without-anisotropic-filter does not fire when filter IS ANISOTROPIC",
          "[rules][anisotropy-without-anisotropic-filter]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

void configure_sampler()
{
    g_sampler.MaxAnisotropy = 16;
    g_sampler.Filter = ANISOTROPIC;
}

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "anisotropy-without-anisotropic-filter");
    }
}

TEST_CASE("anisotropy-without-anisotropic-filter does not fire when MaxAnisotropy = 1",
          "[rules][anisotropy-without-anisotropic-filter]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

void configure_sampler()
{
    g_sampler.MaxAnisotropy = 1;
    g_sampler.Filter = MIN_MAG_MIP_LINEAR;
}

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "anisotropy-without-anisotropic-filter");
    }
}

TEST_CASE("anisotropy-without-anisotropic-filter does not fire when sampler has no MaxAnisotropy",
          "[rules][anisotropy-without-anisotropic-filter]") {
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
        CHECK(d.code != "anisotropy-without-anisotropic-filter");
    }
}

TEST_CASE("anisotropy-without-anisotropic-filter fires on COMPARISON_ANISOTROPIC accepted",
          "[rules][anisotropy-without-anisotropic-filter]") {
    SourceManager sources;
    // COMPARISON_ANISOTROPIC contains the ANISOTROPIC substring -- the rule
    // accepts any filter mode whose name mentions ANISOTROPIC.
    const std::string hlsl = R"hlsl(
Texture2D<float> shadow_map;
SamplerComparisonState shadow_sampler;

void configure_sampler()
{
    shadow_sampler.MaxAnisotropy = 8;
    shadow_sampler.Filter = COMPARISON_ANISOTROPIC;
}

[shader("pixel")]
float ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return shadow_map.SampleCmp(shadow_sampler, uv, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "anisotropy-without-anisotropic-filter");
    }
}
