// End-to-end tests for the manual-srgb-conversion rule.
//
// This rule is a forward-compatible stub: its emit is gated behind a DXGI
// format check that today's reflection bridge does not expose. The tests
// document this state -- the rule must NOT fire today even on the
// canonical `pow(x, 2.2)` pattern, because we cannot tell whether the
// underlying texture's format already carries the sRGB conversion.

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
[[nodiscard]] std::unique_ptr<Rule> make_manual_srgb_conversion();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_manual_srgb_conversion());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}

}  // namespace

TEST_CASE("manual-srgb-conversion is a forward-compatible stub: never fires today on pow(x, 2.2)",
          "[rules][manual-srgb-conversion]") {
    // The rule walks the AST for `pow(...,2.2)` calls but gates the emit
    // behind a DXGI format check that the bridge does not yet expose.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    float4 c = base_color.Sample(g_sampler, uv);
    c.rgb = pow(c.rgb, 2.2);
    return c;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "manual-srgb-conversion");
    }
}

TEST_CASE("manual-srgb-conversion does not crash on shader without pow calls",
          "[rules][manual-srgb-conversion]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "manual-srgb-conversion");
    }
}
