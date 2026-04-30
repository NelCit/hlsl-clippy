// End-to-end tests for the manual-reflect rule.
// v - 2 * dot(n, v) * n should be replaced with reflect(v, n).

#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "test_config.hpp"

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

TEST_CASE("manual-reflect fires on v - 2.0 * dot(n, v) * n", "[rules][manual-reflect]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 n) {
    return v - 2.0 * dot(n, v) * n;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-reflect"));
}

TEST_CASE("manual-reflect fires on v - 2 * dot(n, v) * n (integer literal 2)",
          "[rules][manual-reflect]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 n) {
    return v - 2 * dot(n, v) * n;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-reflect"));
}

TEST_CASE("manual-reflect does not fire when n identifiers differ",
          "[rules][manual-reflect]") {
    SourceManager sources;
    // Different n on lhs of dot and trailing multiply.
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 n, float3 m) {
    return v - 2.0 * dot(m, v) * n;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-reflect");
}

TEST_CASE("manual-reflect does not fire when v identifiers differ",
          "[rules][manual-reflect]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 w, float3 n) {
    return v - 2.0 * dot(n, w) * n;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-reflect");
}

TEST_CASE("manual-reflect does not fire when coefficient is not 2",
          "[rules][manual-reflect]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 n) {
    return v - 3.0 * dot(n, v) * n;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-reflect");
}

TEST_CASE("manual-reflect fix is suggestion-only", "[rules][manual-reflect][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 n) {
    return v - 2.0 * dot(n, v) * n;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "manual-reflect") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    // The suggested replacement should mention reflect
    CHECK(hit->fixes[0].edits[0].replacement.find("reflect") != std::string::npos);
}

TEST_CASE("manual-reflect fires on math.hlsl fixture", "[rules][manual-reflect][fixture]") {
    std::filesystem::path fixture{std::string{hlsl_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "math.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    std::size_t count = 0;
    for (const auto& d : diags) {
        if (d.code == "manual-reflect") ++count;
    }
    CHECK(count >= 1U);
}
