// End-to-end tests for the sin-cos-pair rule.
// Separate sin(x) and cos(x) in the same scope should be merged with sincos().

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

TEST_CASE("sin-cos-pair fires when sin(x) and cos(x) are both present", "[rules][sin-cos-pair]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float2 f(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2(s, c);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "sin-cos-pair"));
}

TEST_CASE("sin-cos-pair does not fire when sin and cos have different arguments",
          "[rules][sin-cos-pair]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float2 f(float a, float b) {
    float s = sin(a);
    float c = cos(b);
    return float2(s, c);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "sin-cos-pair");
}

TEST_CASE("sin-cos-pair does not fire with only sin (no paired cos)", "[rules][sin-cos-pair]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float angle) {
    return sin(angle);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "sin-cos-pair");
}

TEST_CASE("sin-cos-pair does not fire with only cos (no paired sin)", "[rules][sin-cos-pair]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float angle) {
    return cos(angle);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "sin-cos-pair");
}

TEST_CASE("sin-cos-pair fix is suggestion-only", "[rules][sin-cos-pair][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float2 f(float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2(s, c);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "sin-cos-pair") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}

TEST_CASE("sin-cos-pair fires on math.hlsl fixture", "[rules][sin-cos-pair][fixture]") {
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
        if (d.code == "sin-cos-pair") ++count;
    }
    CHECK(count >= 1U);
}

TEST_CASE("sin-cos-pair fires on realistic_pbr_vs.hlsl fixture",
          "[rules][sin-cos-pair][fixture]") {
    std::filesystem::path fixture{std::string{hlsl_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "realistic_pbr_vs.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    std::size_t count = 0;
    for (const auto& d : diags) {
        if (d.code == "sin-cos-pair") ++count;
    }
    CHECK(count >= 1U);
}

TEST_CASE("sin-cos-pair does not fire on negative_lookalikes fixture",
          "[rules][sin-cos-pair][fixture]") {
    std::filesystem::path fixture{std::string{hlsl_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "negative_lookalikes.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    for (const auto& d : diags) CHECK(d.code != "sin-cos-pair");
}
