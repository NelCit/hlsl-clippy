// End-to-end tests for the forcecase-missing-on-ps-switch rule (ADR 0011
// §Phase 4 pack C, ADR 0013 sub-phase 4c). The rule fires on a `switch`
// inside a PS entry point whose case bodies use derivative-bearing
// intrinsics and which lacks the `[forcecase]` attribute.
//
// Coverage:
//   * PS function (SV_Target return) with switch + Sample => fires.
//   * Same with `[forcecase]` => does not fire.
//   * PS function with switch but no derivatives => does not fire.
//   * Compute function with switch + Sample => does not fire (not PS).
//   * `[shader("pixel")]` annotated function with switch + ddx => fires.

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
[[nodiscard]] std::unique_ptr<Rule> make_forcecase_missing_on_ps_switch();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_forcecase_missing_on_ps_switch());
    return lint(sources, src, rules);
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

TEST_CASE("forcecase-missing-on-ps-switch fires on PS switch with Sample",
          "[rules][forcecase-missing-on-ps-switch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> g_AlbedoA;
Texture2D<float4> g_AlbedoB;
SamplerState g_Samp;

float4 ps_per_material(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target
{
    switch (matId) {
        case 0: return g_AlbedoA.Sample(g_Samp, uv);
        case 1: return g_AlbedoB.Sample(g_Samp, uv);
        default: return float4(0, 0, 0, 1);
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "forcecase-missing-on-ps-switch"));
}

TEST_CASE("forcecase-missing-on-ps-switch does not fire when [forcecase] is present",
          "[rules][forcecase-missing-on-ps-switch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> g_AlbedoA;
Texture2D<float4> g_AlbedoB;
SamplerState g_Samp;

float4 ps_per_material_safe(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target
{
    [forcecase]
    switch (matId) {
        case 0: return g_AlbedoA.Sample(g_Samp, uv);
        case 1: return g_AlbedoB.Sample(g_Samp, uv);
        default: return float4(0, 0, 0, 1);
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK_FALSE(has_rule(diags, "forcecase-missing-on-ps-switch"));
}

TEST_CASE("forcecase-missing-on-ps-switch does not fire on PS switch without derivatives",
          "[rules][forcecase-missing-on-ps-switch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float4 ps_simple(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target
{
    switch (matId) {
        case 0: return float4(1, 0, 0, 1);
        case 1: return float4(0, 1, 0, 1);
        default: return float4(0, 0, 0, 1);
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK_FALSE(has_rule(diags, "forcecase-missing-on-ps-switch"));
}

TEST_CASE("forcecase-missing-on-ps-switch does not fire on compute switch with Sample",
          "[rules][forcecase-missing-on-ps-switch]") {
    SourceManager sources;
    // No SV_Target / no [shader("pixel")] => not a PS entry point.
    const std::string hlsl = R"hlsl(
Texture2D<float4> g_Tex;
SamplerState g_Samp;
RWStructuredBuffer<float4> g_Out;

[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v;
    switch (tid.x) {
        case 0: v = g_Tex.Sample(g_Samp, float2(0, 0)); break;
        case 1: v = g_Tex.Sample(g_Samp, float2(1, 1)); break;
        default: v = float4(0, 0, 0, 1); break;
    }
    g_Out[tid.x] = v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK_FALSE(has_rule(diags, "forcecase-missing-on-ps-switch"));
}

TEST_CASE("forcecase-missing-on-ps-switch fires on [shader(\"pixel\")] function with ddx",
          "[rules][forcecase-missing-on-ps-switch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_ddx(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target
{
    float4 result = float4(0, 0, 0, 1);
    switch (matId) {
        case 0: result.x = ddx(uv.x); break;
        case 1: result.y = ddx(uv.y); break;
        default: break;
    }
    return result;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "forcecase-missing-on-ps-switch"));
}
