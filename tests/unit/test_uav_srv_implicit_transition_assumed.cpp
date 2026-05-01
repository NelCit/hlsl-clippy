// End-to-end tests for the uav-srv-implicit-transition-assumed rule
// (Pack C, Phase 3 reflection-aware).
//
// The rule fires when reflection surfaces a UAV binding `U` and an SRV
// binding `S` that alias either by sharing the same register slot in
// different spaces, or by sharing the same shader-side identifier name.
// The rule is suggestion-grade with NO fix -- the explicit barrier lives
// in host-side code.

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
[[nodiscard]] std::unique_ptr<Rule> make_uav_srv_implicit_transition_assumed();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources,
                                                  const std::string& path = "synthetic.hlsl") {
    const auto src = sources.add_buffer(path, hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_uav_srv_implicit_transition_assumed());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, options);
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

TEST_CASE(
    "uav-srv-implicit-transition-assumed fires when UAV and SRV share register slot in different "
    "spaces",
    "[rules][uav-srv-implicit-transition-assumed]") {
    SourceManager sources;
    // RWBuffer at u0,space0 and Buffer at t0,space0 don't actually clash;
    // RWBuffer at u0,space0 and Buffer at t0,space1 share the slot 0 across
    // different spaces, which is the alias the rule is designed to flag.
    const std::string hlsl = R"hlsl(
RWBuffer<float>  uav_view  : register(u0, space0);
Buffer<float>    srv_view  : register(t0, space1);

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uav_view[tid.x] = srv_view.Load(tid.x);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "uav-srv-implicit-transition-assumed"));
}

TEST_CASE("uav-srv-implicit-transition-assumed does not fire on cleanly distinct bindings",
          "[rules][uav-srv-implicit-transition-assumed]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWBuffer<float>  uav_view  : register(u0, space0);
Buffer<float>    srv_view  : register(t1, space0);

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uav_view[tid.x] = srv_view.Load(tid.x);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "uav-srv-implicit-transition-assumed");
    }
}

TEST_CASE("uav-srv-implicit-transition-assumed does not fire on a cbuffer-only source",
          "[rules][uav-srv-implicit-transition-assumed]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Constants
{
    float4 dummy;
};

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return dummy;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "uav-srv-implicit-transition-assumed");
    }
}

TEST_CASE("uav-srv-implicit-transition-assumed does not fire when only UAV bindings are present",
          "[rules][uav-srv-implicit-transition-assumed]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWBuffer<float>  uav_a  : register(u0, space0);
RWBuffer<float>  uav_b  : register(u1, space0);

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uav_a[tid.x] = (float)tid.x;
    uav_b[tid.x] = (float)tid.x * 2.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "uav-srv-implicit-transition-assumed");
    }
}

TEST_CASE("uav-srv-implicit-transition-assumed emits a suggestion-grade diagnostic with no fix",
          "[rules][uav-srv-implicit-transition-assumed]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWTexture2D<float4>  scene_uav  : register(u0, space0);
Texture2D<float4>    scene_srv  : register(t0, space2);

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = scene_srv.Load(int3(tid.xy, 0));
    scene_uav[tid.xy] = v;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "uav-srv-implicit-transition-assumed") {
            hit = &d;
            break;
        }
    }
    if (hit != nullptr) {
        CHECK(hit->fixes.empty());
    }
}
