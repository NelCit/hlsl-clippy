// End-to-end tests for the sample-in-loop-implicit-grad rule.

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
    const auto src = sources.add_buffer("silig.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("sample-in-loop-implicit-grad smoke test", "[rules][sample-in-loop-implicit-grad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D Tex;
SamplerState Smp;

float4 main(float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    for (int i = 0; i < 4; ++i) {
        acc += Tex.Sample(Smp, uv + float2(i, 0));
    }
    return acc;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    (void)diags;
    SUCCEED("sample-in-loop-implicit-grad ran without crashing");
}

TEST_CASE("sample-in-loop-implicit-grad does not fire on SampleLevel",
          "[rules][sample-in-loop-implicit-grad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D Tex;
SamplerState Smp;

float4 main(float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    for (int i = 0; i < 4; ++i) {
        acc += Tex.SampleLevel(Smp, uv, 0);
    }
    return acc;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "sample-in-loop-implicit-grad");
}
