// End-to-end tests for the manual-refract rule.
// The closed-form refract body should be flagged as a suggestion-only fix.

#include <string>
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

}  // namespace

TEST_CASE("manual-refract fires on closed-form body with hoisted k",
          "[rules][manual-refract]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 hand_rolled_refract(float3 I, float3 N, float eta) {
    float k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I));
    return eta * I - (eta * dot(N, I) + sqrt(k)) * N;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-refract"));
}

TEST_CASE("manual-refract fires on closed-form body with inlined sqrt argument",
          "[rules][manual-refract]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 hand_rolled_refract(float3 I, float3 N, float eta) {
    return eta * I - (eta * dot(N, I) + sqrt(1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I)))) * N;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-refract"));
}

TEST_CASE("manual-refract does not fire on a plain refract() call",
          "[rules][manual-refract]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 use_intrinsic(float3 I, float3 N, float eta) {
    return refract(I, N, eta);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-refract");
}

TEST_CASE("manual-refract does not fire when the subtraction is missing",
          "[rules][manual-refract]") {
    SourceManager sources;
    // Has sqrt and dot, but no top-level subtraction in the return expression.
    const std::string hlsl = R"hlsl(
float fake(float3 I, float3 N) {
    return sqrt(dot(N, I));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-refract");
}

TEST_CASE("manual-refract does not fire when sqrt is absent",
          "[rules][manual-refract]") {
    SourceManager sources;
    // Looks vaguely like reflect-ish maths but lacks the sqrt() marker.
    const std::string hlsl = R"hlsl(
float3 not_refract(float3 I, float3 N, float eta) {
    return eta * I - (eta * dot(N, I) + 1.0) * N;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-refract");
}

TEST_CASE("manual-refract does not fire on length-of-difference",
          "[rules][manual-refract]") {
    SourceManager sources;
    // Triggers manual-distance, not manual-refract.
    const std::string hlsl = R"hlsl(
float dist(float3 a, float3 b) {
    return length(a - b);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-refract");
}

TEST_CASE("manual-refract fix is suggestion-only and mentions refract",
          "[rules][manual-refract][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 hand_rolled_refract(float3 I, float3 N, float eta) {
    float k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I));
    return eta * I - (eta * dot(N, I) + sqrt(k)) * N;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "manual-refract") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    CHECK(hit->fixes[0].description.find("refract") != std::string::npos);
}
