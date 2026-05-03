// Tests for the redundant-texture-sample rule (Phase 7 Pack Pressure; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_redundant_texture_sample();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("rts.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_redundant_texture_sample());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("redundant-texture-sample smoke (does not crash)",
          "[rules][redundant-texture-sample]") {
    const std::string hlsl = R"hlsl(
Texture2D<float4> Tex;
SamplerState SS;

float4 ps_main(float2 uv : TEXCOORD) : SV_Target {
    float4 a = Tex.Sample(SS, uv);
    float4 b = Tex.Sample(SS, uv);
    return lerp(a, b, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
    SUCCEED("redundant-texture-sample ran without crashing");
}

TEST_CASE("redundant-texture-sample silent on samples with different UVs",
          "[rules][redundant-texture-sample]") {
    const std::string hlsl = R"hlsl(
Texture2D<float4> Tex;
SamplerState SS;

float4 ps_main(float2 uv0 : TEXCOORD0, float2 uv1 : TEXCOORD1) : SV_Target {
    float4 a = Tex.Sample(SS, uv0);
    float4 b = Tex.Sample(SS, uv1);
    return lerp(a, b, 0.5);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "redundant-texture-sample");
}
