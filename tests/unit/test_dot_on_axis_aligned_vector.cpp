// End-to-end tests for the dot-on-axis-aligned-vector rule.
// dot(v, float3(1, 0, 0)) -> v.x (and the y/z/w variants), machine-applicable.

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

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("dot-on-axis-aligned-vector fires on dot(v, float3(1, 0, 0))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(1, 0, 0)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dot-on-axis-aligned-vector"));
}

TEST_CASE("dot-on-axis-aligned-vector fires on dot(v, float3(0, 1, 0))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(0, 1, 0)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dot-on-axis-aligned-vector"));
}

TEST_CASE("dot-on-axis-aligned-vector fires on dot(v, float3(0, 0, 1))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(0, 0, 1)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dot-on-axis-aligned-vector"));
}

TEST_CASE("dot-on-axis-aligned-vector fires on dot(v, float4(0, 0, 0, 1))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float4 v) { return dot(v, float4(0, 0, 0, 1)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dot-on-axis-aligned-vector"));
}

TEST_CASE("dot-on-axis-aligned-vector fires on dot(float3(1, 0, 0), v) (commutative)",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(float3(1, 0, 0), v); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "dot-on-axis-aligned-vector"));
}

TEST_CASE("dot-on-axis-aligned-vector does not fire on dot(v, float3(1, 1, 0))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(1, 1, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "dot-on-axis-aligned-vector");
}

TEST_CASE("dot-on-axis-aligned-vector does not fire on dot(v, float3(-1, 0, 0))",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(-1, 0, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "dot-on-axis-aligned-vector");
}

TEST_CASE("dot-on-axis-aligned-vector does not fire on dot(a, b)",
          "[rules][dot-on-axis-aligned-vector]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return dot(a, b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "dot-on-axis-aligned-vector");
}

TEST_CASE("dot-on-axis-aligned-vector fix replaces with v.x",
          "[rules][dot-on-axis-aligned-vector][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(1, 0, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "dot-on-axis-aligned-vector");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "v.x");
}

TEST_CASE("dot-on-axis-aligned-vector fix for axis y replaces with v.y",
          "[rules][dot-on-axis-aligned-vector][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return dot(v, float3(0, 1, 0)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "dot-on-axis-aligned-vector");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "v.y");
}
