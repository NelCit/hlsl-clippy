// End-to-end tests for missing-ray-flag-cull-non-opaque.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("mrfc.hlsl", hlsl);
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

TEST_CASE("missing-ray-flag-cull-non-opaque fires on TraceRay without flag",
          "[rules][missing-ray-flag-cull-non-opaque]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure g_BVH;

struct ShadowPayload { float v; };

[shader("raygeneration")]
void RayGen() {
    RayDesc ray;
    ShadowPayload p = { 1.0 };
    TraceRay(g_BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "missing-ray-flag-cull-non-opaque"));
}

TEST_CASE("missing-ray-flag-cull-non-opaque silent when flag present",
          "[rules][missing-ray-flag-cull-non-opaque]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure g_BVH;

struct ShadowPayload { float v; };

[shader("raygeneration")]
void RayGen() {
    RayDesc ray;
    ShadowPayload p = { 1.0 };
    TraceRay(g_BVH, RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "missing-ray-flag-cull-non-opaque"));
}

TEST_CASE("missing-ray-flag-cull-non-opaque attaches an OR-into-flag Fix",
          "[rules][missing-ray-flag-cull-non-opaque][fix]") {
    // Suggestion-grade: the rule cannot prove the application doesn't expect
    // this trace to visit alpha-tested geometry. The textual rewrite ORs the
    // missing flag into whatever the existing flag expression spells.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RaytracingAccelerationStructure g_BVH;

struct ShadowPayload { float v; };

[shader("raygeneration")]
void RayGen() {
    RayDesc ray;
    ShadowPayload p = { 1.0 };
    TraceRay(g_BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 1, 0, ray, p);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "missing-ray-flag-cull-non-opaque") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK_FALSE(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement ==
              "RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE");
    }
    CHECK(saw);
}
