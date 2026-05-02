// Tests for ray-flag-force-opaque-with-anyhit (Phase 8 v0.9 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_ray_flag_force_opaque_with_anyhit();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("rfoa.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_ray_flag_force_opaque_with_anyhit());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, opts);
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

TEST_CASE("ray-flag-force-opaque-with-anyhit fires when both are present",
          "[rules][ray-flag-force-opaque-with-anyhit]") {
    const std::string hlsl = R"hlsl(
struct Payload { float3 c; };
struct Attrs { float2 b; };

[shader("anyhit")]
void ah(inout Payload p, in Attrs a) {
    p.c = float3(0, 0, 0);
}

[shader("raygeneration")]
void rg() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "ray-flag-force-opaque-with-anyhit"));
}

TEST_CASE("ray-flag-force-opaque-with-anyhit silent without an anyhit shader",
          "[rules][ray-flag-force-opaque-with-anyhit]") {
    const std::string hlsl = R"hlsl(
struct Payload { float3 c; };

[shader("raygeneration")]
void rg() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "ray-flag-force-opaque-with-anyhit"));
}
