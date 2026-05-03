// Tests for the wave-reduction-pixel-without-helper-attribute rule (Phase 4
// Pack E). Forward-compatible-stub: textual co-occurrence of a `WaveActive*`
// reduction with a derivative consumer in a pixel-shader entry that lacks
// `[WaveOpsIncludeHelperLanes]`.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_wave_reduction_pixel_without_helper_attribute();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_wave_reduction_pixel_without_helper_attribute());
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

TEST_CASE("wave-reduction-pixel-without-helper-attribute fires on PS reduction + ddx",
          "[rules][wave-reduction-pixel-without-helper-attribute]") {
    const std::string hlsl = R"hlsl(
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x);
    float dudx    = ddx(waveAvg);
    return float4(dudx, 0, 0, 0);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "wave-reduction-pixel-without-helper-attribute"));
}

TEST_CASE("wave-reduction-pixel-without-helper-attribute is silent with the attribute",
          "[rules][wave-reduction-pixel-without-helper-attribute]") {
    const std::string hlsl = R"hlsl(
[WaveOpsIncludeHelperLanes]
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x);
    float dudx    = ddx(waveAvg);
    return float4(dudx, 0, 0, 0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "wave-reduction-pixel-without-helper-attribute");
    }
}

TEST_CASE("wave-reduction-pixel-without-helper-attribute is silent without a derivative",
          "[rules][wave-reduction-pixel-without-helper-attribute]") {
    const std::string hlsl = R"hlsl(
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x);
    return float4(waveAvg, 0, 0, 0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "wave-reduction-pixel-without-helper-attribute");
    }
}
