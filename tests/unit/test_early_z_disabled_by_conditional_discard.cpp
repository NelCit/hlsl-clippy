// End-to-end tests for the early-z-disabled-by-conditional-discard rule.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("ezcd.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("early-z-disabled-by-conditional-discard smoke test",
          "[rules][early-z-disabled-by-conditional-discard]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D Tex;
SamplerState Smp;

float4 main(float2 uv : TEXCOORD0) : SV_Target {
    float a = Tex.SampleLevel(Smp, uv, 0).a;
    if (a < 0.5) {
        discard;
    }
    return float4(1, 1, 1, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    (void)diags;
    SUCCEED("early-z-disabled-by-conditional-discard ran without crashing");
}

TEST_CASE("early-z-disabled-by-conditional-discard does not fire when [earlydepthstencil] is set",
          "[rules][early-z-disabled-by-conditional-discard]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D Tex;
SamplerState Smp;

[earlydepthstencil]
float4 main(float2 uv : TEXCOORD0) : SV_Target {
    float a = Tex.SampleLevel(Smp, uv, 0).a;
    if (a < 0.5) {
        discard;
    }
    return float4(1, 1, 1, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "early-z-disabled-by-conditional-discard");
}
