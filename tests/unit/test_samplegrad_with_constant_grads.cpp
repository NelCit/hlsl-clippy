// End-to-end tests for samplegrad-with-constant-grads.

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
    const auto src = sources.add_buffer("sgcg.hlsl", hlsl);
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

TEST_CASE("samplegrad-with-constant-grads fires on zero gradients",
          "[rules][samplegrad-with-constant-grads]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.SampleGrad(s, uv, float2(0,0), float2(0,0));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "samplegrad-with-constant-grads"));
}

TEST_CASE("samplegrad-with-constant-grads does not fire on real gradients",
          "[rules][samplegrad-with-constant-grads]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    return t.SampleGrad(s, uv, dx, dy);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "samplegrad-with-constant-grads"));
}

TEST_CASE("samplegrad-with-constant-grads attaches a SampleLevel rewrite Fix",
          "[rules][samplegrad-with-constant-grads][fix]") {
    // Regression for v0.6.8: rule was tagged `applicability: machine-applicable`
    // in docs but emitted a Fix-less diagnostic. Verify the rewrite reconstructs
    // a well-formed `<tex>.SampleLevel(<sampler>, <uv>, 0.0)` call.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.SampleGrad(s, uv, float2(0,0), float2(0,0));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "samplegrad-with-constant-grads") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        // tex/sampler/uv are all bare identifiers → machine-applicable.
        CHECK(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        const auto& replacement = fix.edits.front().replacement;
        CHECK(replacement == "t.SampleLevel(s, uv, 0.0)");
    }
    CHECK(saw);
}
