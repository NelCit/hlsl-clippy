// End-to-end tests for gather-cmp-vs-manual-pcf.

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
    const auto src = sources.add_buffer("gcvp.hlsl", hlsl);
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

TEST_CASE("gather-cmp-vs-manual-pcf fires on 4 SampleCmp calls",
          "[rules][gather-cmp-vs-manual-pcf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D                  shadow : register(t0);
SamplerComparisonState     pcf    : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, float zRef : TEXCOORD1) : SV_Target {
    float a = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    float b = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    float c = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    float d = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    return float4(a + b + c + d, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "gather-cmp-vs-manual-pcf"));
}

TEST_CASE("gather-cmp-vs-manual-pcf does not fire on a single SampleCmp",
          "[rules][gather-cmp-vs-manual-pcf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D                  shadow : register(t0);
SamplerComparisonState     pcf    : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, float zRef : TEXCOORD1) : SV_Target {
    float v = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    return float4(v, v, v, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "gather-cmp-vs-manual-pcf"));
}
