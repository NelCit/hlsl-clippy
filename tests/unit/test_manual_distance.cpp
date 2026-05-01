// End-to-end tests for the manual-distance rule.
// length(a - b) -> distance(a, b), machine-applicable.

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

TEST_CASE("manual-distance fires on length(a - b)", "[rules][manual-distance]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return length(a - b); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-distance"));
}

TEST_CASE("manual-distance fires on length(p1 - p2)", "[rules][manual-distance]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 p1, float3 p2) { return length(p1 - p2); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-distance"));
}

TEST_CASE("manual-distance does not fire on length(a)", "[rules][manual-distance]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a) { return length(a); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-distance");
}

TEST_CASE("manual-distance does not fire on length(a + b)", "[rules][manual-distance]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return length(a + b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-distance");
}

TEST_CASE("manual-distance does not fire on plain distance(a, b)",
          "[rules][manual-distance]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return distance(a, b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-distance");
}

TEST_CASE("manual-distance fix replaces with distance(a, b)",
          "[rules][manual-distance][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return length(a - b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "manual-distance");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "distance(a, b)");
}
