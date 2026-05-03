// Tests for the live-state-across-traceray rule (Phase 7 Pack DXR; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_live_state_across_traceray();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("lstr.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_live_state_across_traceray());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("live-state-across-traceray smoke (does not crash)",
          "[rules][live-state-across-traceray]") {
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure scene;
struct P { float3 c; };

float3 raygen_helper(float3 origin) {
    float a = origin.x + 1.0;
    float b = origin.y + 2.0;
    float c = origin.z + 3.0;
    float d = origin.x * 4.0;
    P p;
    p.c = float3(a, b, c);
    RayDesc r;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
    return p.c + float3(a, b, d);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
    SUCCEED("live-state-across-traceray ran without crashing");
}

TEST_CASE("live-state-across-traceray silent when no TraceRay is present",
          "[rules][live-state-across-traceray]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target {
    float a = 1.0;
    float b = 2.0;
    float c = 3.0;
    return float4(a, b, c, 1.0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "live-state-across-traceray");
}
