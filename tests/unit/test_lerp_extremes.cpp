// End-to-end tests for the lerp-extremes rule.
// lerp(a, b, 0) -> a and lerp(a, b, 1) -> b, machine-applicable.

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "test_config.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

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

TEST_CASE("lerp-extremes fires on lerp(a, b, 0.0)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) {
    return lerp(a, b, 0.0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "lerp-extremes"));
}

TEST_CASE("lerp-extremes fires on lerp(a, b, 1.0)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) {
    return lerp(a, b, 1.0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "lerp-extremes"));
}

TEST_CASE("lerp-extremes fires on lerp(a, b, 0) (integer literal)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return lerp(a, b, 0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "lerp-extremes"));
}

TEST_CASE("lerp-extremes fires on lerp(a, b, 1) (integer literal)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return lerp(a, b, 1); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "lerp-extremes"));
}

TEST_CASE("lerp-extremes does not fire on lerp(a, b, 0.5)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return lerp(a, b, 0.5); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "lerp-extremes");
    }
}

TEST_CASE("lerp-extremes does not fire on lerp(a, b, t)", "[rules][lerp-extremes]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b, float t) { return lerp(a, b, t); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "lerp-extremes");
    }
}

TEST_CASE("lerp-extremes fix for t==0 replaces with first argument", "[rules][lerp-extremes][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return lerp(a, b, 0.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "lerp-extremes") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "a");
}

TEST_CASE("lerp-extremes fix for t==1 replaces with second argument", "[rules][lerp-extremes][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return lerp(a, b, 1.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "lerp-extremes") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "b");
}

TEST_CASE("lerp-extremes fires on the phase2 math fixture", "[rules][lerp-extremes][fixture]") {
    std::filesystem::path fixture{std::string{shader_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "math.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    std::vector<unsigned> hit_lines;
    for (const auto& d : diags) {
        if (d.code == "lerp-extremes") {
            const auto loc = sources.resolve(d.primary_span.source, d.primary_span.bytes.lo);
            hit_lines.push_back(loc.line);
        }
    }
    // math.hlsl lines 37 and 42 carry the HIT annotations (lerp zero and one endpoints).
    const auto contains = [&](unsigned ln) {
        return std::ranges::find(hit_lines, ln) != hit_lines.end();
    };
    // Fixture has two hits: lerp(a, b, 0.0) and lerp(a, b, 1.0)
    CHECK(hit_lines.size() >= 2U);
    (void)contains;
}

TEST_CASE("lerp-extremes does not fire on negative_lookalikes fixture",
          "[rules][lerp-extremes][fixture]") {
    std::filesystem::path fixture{std::string{shader_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "negative_lookalikes.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    for (const auto& d : diags) {
        CHECK(d.code != "lerp-extremes");
    }
}
