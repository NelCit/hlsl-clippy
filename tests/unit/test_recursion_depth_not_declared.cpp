// Tests for the recursion-depth-not-declared rule (Phase 7 Pack DXR; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_recursion_depth_not_declared();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("rdn.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_recursion_depth_not_declared());
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

TEST_CASE("recursion-depth-not-declared fires on raygen with TraceRay and no max-depth",
          "[rules][recursion-depth-not-declared]") {
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure scene;
struct P { float3 c; };

[shader("raygeneration")]
void RayGen() {
    P p;
    p.c = 0;
    RayDesc r;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "recursion-depth-not-declared"));
}

TEST_CASE("recursion-depth-not-declared silent when MaxRecursionDepth is mentioned",
          "[rules][recursion-depth-not-declared]") {
    const std::string hlsl = R"hlsl(
// Pipeline-side declaration: MaxRecursionDepth = 4
RaytracingAccelerationStructure scene;
struct P { float3 c; };

[shader("raygeneration")]
void RayGen() {
    P p;
    p.c = 0;
    RayDesc r;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "recursion-depth-not-declared"));
}

TEST_CASE("recursion-depth-not-declared silent on non-raygen entries",
          "[rules][recursion-depth-not-declared]") {
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "recursion-depth-not-declared"));
}
