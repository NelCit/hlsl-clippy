// End-to-end tests for the cross-with-up-vector rule.
// cross(v, K) where K is an axis-aligned constant unit vector collapses to
// a swizzle plus a sign flip; machine-applicable.

#include <string>
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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("cross-with-up-vector fires on cross(v, float3(0, 1, 0))",
          "[rules][cross-with-up-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(v, float3(0, 1, 0)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "cross-with-up-vector"));
}

TEST_CASE("cross-with-up-vector fires on cross(v, float3(1, 0, 0))",
          "[rules][cross-with-up-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(v, float3(1, 0, 0)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "cross-with-up-vector"));
}

TEST_CASE("cross-with-up-vector fires on cross(float3(0, 0, 1), v)",
          "[rules][cross-with-up-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(float3(0, 0, 1), v); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "cross-with-up-vector"));
}

TEST_CASE("cross-with-up-vector does not fire on cross(v, float3(1, 1, 0))",
          "[rules][cross-with-up-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(v, float3(1, 1, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "cross-with-up-vector");
}

TEST_CASE("cross-with-up-vector does not fire on cross(a, b)",
          "[rules][cross-with-up-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return cross(a, b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "cross-with-up-vector");
}

TEST_CASE("cross-with-up-vector fix for cross(v, float3(0, 1, 0)) is float3(-v.z, 0, v.x)",
          "[rules][cross-with-up-vector][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(v, float3(0, 1, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "cross-with-up-vector");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "float3(-v.z, 0, v.x)");
}

TEST_CASE("cross-with-up-vector fix for cross(v, float3(1, 0, 0)) is float3(0, v.z, -v.y)",
          "[rules][cross-with-up-vector][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(v, float3(1, 0, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "cross-with-up-vector");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "float3(0, v.z, -v.y)");
}

TEST_CASE("cross-with-up-vector fix for cross(float3(0, 1, 0), v) flips signs",
          "[rules][cross-with-up-vector][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return cross(float3(0, 1, 0), v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "cross-with-up-vector");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    // cross(K, v) = -cross(v, K); for K = (0,1,0) the rhs identity is
    // (-v.z, 0, v.x); flipped: (v.z, 0, -v.x).
    CHECK(hit->fixes[0].edits[0].replacement == "float3(v.z, 0, -v.x)");
}
