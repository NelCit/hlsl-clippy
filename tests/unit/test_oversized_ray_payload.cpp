// Tests for the oversized-ray-payload rule (Phase 7 Pack DXR; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_oversized_ray_payload();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("orp.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_oversized_ray_payload());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, opts);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("oversized-ray-payload fires on a struct above 32 bytes in a DXR-flavoured TU",
          "[rules][oversized-ray-payload]") {
    const std::string hlsl = R"hlsl(
struct BigPayload {
    float3 radiance;
    float3 throughput;
    float3 origin;
    float3 direction;
    float4 albedo;
    uint depth;
};

[shader("raygeneration")]
void RayGen() {
    BigPayload p;
    p.radiance = float3(0,0,0);
    RaytracingAccelerationStructure scene;
    RayDesc r;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "oversized-ray-payload"));
}

TEST_CASE("oversized-ray-payload silent on a small payload",
          "[rules][oversized-ray-payload]") {
    const std::string hlsl = R"hlsl(
struct SmallPayload {
    float3 radiance;
    uint flags;
};

[shader("raygeneration")]
void RayGen() {
    SmallPayload p;
    p.radiance = float3(0,0,0);
    RaytracingAccelerationStructure scene;
    RayDesc r;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "oversized-ray-payload"));
}

TEST_CASE("oversized-ray-payload silent on a non-DXR translation unit",
          "[rules][oversized-ray-payload]") {
    const std::string hlsl = R"hlsl(
struct BigBlob {
    float4 a;
    float4 b;
    float4 c;
};
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "oversized-ray-payload"));
}
