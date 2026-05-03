// End-to-end tests for samplecmp-vs-manual-compare.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("scvc.hlsl", hlsl);
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

TEST_CASE("samplecmp-vs-manual-compare fires on Sample then compare",
          "[rules][samplecmp-vs-manual-compare]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    depth : register(t0);
SamplerState s     : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, float zRef : TEXCOORD1) : SV_Target {
    float visible = depth.Sample(s, uv).x < zRef ? 0.0 : 1.0;
    return float4(visible, visible, visible, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "samplecmp-vs-manual-compare"));
}

TEST_CASE("samplecmp-vs-manual-compare does not fire on plain Sample",
          "[rules][samplecmp-vs-manual-compare]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.Sample(s, uv);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "samplecmp-vs-manual-compare"));
}
