// End-to-end tests for texture-as-buffer.

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
    const auto src = sources.add_buffer("tab.hlsl", hlsl);
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

TEST_CASE("texture-as-buffer fires when y is always zero", "[rules][texture-as-buffer]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    LUT : register(t0);
SamplerState s   : register(s0);

[shader("pixel")]
float4 ps_main(float u : TEXCOORD0) : SV_Target {
    return LUT.Sample(s, float2(u, 0));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "texture-as-buffer"));
}

TEST_CASE("texture-as-buffer does not fire on real 2D usage", "[rules][texture-as-buffer]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    Tex : register(t0);
SamplerState s   : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return Tex.Sample(s, uv);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "texture-as-buffer"));
}
