// Tests for the missing-accept-first-hit rule (Phase 7 Pack DXR; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_missing_accept_first_hit();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("mah.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_missing_accept_first_hit());
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

TEST_CASE("missing-accept-first-hit fires on a shadow-named function without the flag",
          "[rules][missing-accept-first-hit]") {
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure scene;
struct ShadowPayload { float occluded; };

void trace_shadow_ray(float3 o, float3 d, inout ShadowPayload p) {
    RayDesc ray;
    ray.Origin = o;
    ray.Direction = d;
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    TraceRay(scene, RAY_FLAG_CULL_NON_OPAQUE, 0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "missing-accept-first-hit"));
}

TEST_CASE("missing-accept-first-hit silent when the flag is present",
          "[rules][missing-accept-first-hit]") {
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure scene;
struct ShadowPayload { float occluded; };

void trace_shadow_ray(float3 o, float3 d, inout ShadowPayload p) {
    RayDesc ray;
    ray.Origin = o;
    ray.Direction = d;
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    TraceRay(scene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
             0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "missing-accept-first-hit"));
}

TEST_CASE("missing-accept-first-hit silent on non-shadowy function names",
          "[rules][missing-accept-first-hit]") {
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure scene;
struct ColorPayload { float3 c; };

void trace_color_ray(float3 o, float3 d, inout ColorPayload p) {
    RayDesc ray;
    ray.Origin = o;
    ray.Direction = d;
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "missing-accept-first-hit"));
}
