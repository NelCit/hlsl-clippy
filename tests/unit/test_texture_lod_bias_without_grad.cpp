// End-to-end tests for texture-lod-bias-without-grad.

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
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("tlbg.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("texture-lod-bias-without-grad fires in compute shader",
          "[rules][texture-lod-bias-without-grad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D       t  : register(t0);
SamplerState    s  : register(s0);
RWTexture2D<float4> Out : register(u0);

[numthreads(8, 8, 1)]
void cs_main(uint3 dt : SV_DispatchThreadID) {
    Out[dt.xy] = t.SampleBias(s, float2(dt.xy) / 1024.0, 0.5);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "texture-lod-bias-without-grad"));
}

TEST_CASE("texture-lod-bias-without-grad silent in pixel shader",
          "[rules][texture-lod-bias-without-grad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.SampleBias(s, uv, 0.5);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "texture-lod-bias-without-grad"));
}
