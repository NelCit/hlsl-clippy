// End-to-end tests for the bgra-rgba-swizzle-mismatch rule.
//
// This rule is a forward-compatible stub: its emit is gated behind a DXGI
// format check that today's reflection bridge does not expose. The tests
// document this state -- the rule must NOT fire under any circumstance
// today, and exists only so the wiring is in place when the bridge surfaces
// format data.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_bgra_rgba_swizzle_mismatch();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_bgra_rgba_swizzle_mismatch());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
}

}  // namespace

TEST_CASE("bgra-rgba-swizzle-mismatch is a forward-compatible stub: never fires today",
          "[rules][bgra-rgba-swizzle-mismatch]") {
    // The rule is wired up but cannot fire until the SlangBridge surfaces
    // DXGI format on ResourceBinding. We exercise the reflection-stage
    // dispatch with a typical Texture2D<float4> + .rgba swizzle source to
    // confirm zero diagnostics today.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D<float4> base_color;
SamplerState g_sampler;

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return base_color.Sample(g_sampler, uv).rgba;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "bgra-rgba-swizzle-mismatch");
    }
}

TEST_CASE("bgra-rgba-swizzle-mismatch does not crash on empty / minimal source",
          "[rules][bgra-rgba-swizzle-mismatch]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target
{
    return float4(0.0, 0.0, 0.0, 1.0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "bgra-rgba-swizzle-mismatch");
    }
}
