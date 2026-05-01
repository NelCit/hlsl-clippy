// End-to-end tests for missing-precise-on-pcf.

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
    const auto src = sources.add_buffer("mppcf.hlsl", hlsl);
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

TEST_CASE("missing-precise-on-pcf fires on bare SampleCmp", "[rules][missing-precise-on-pcf]") {
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
    CHECK(has_rule(lint_buffer(hlsl, sources), "missing-precise-on-pcf"));
}

TEST_CASE("missing-precise-on-pcf silent when precise is nearby",
          "[rules][missing-precise-on-pcf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D                  shadow : register(t0);
SamplerComparisonState     pcf    : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, float zRef : TEXCOORD1) : SV_Target {
    precise float v = shadow.SampleCmpLevelZero(pcf, uv, zRef);
    return float4(v, v, v, 1);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "missing-precise-on-pcf"));
}
