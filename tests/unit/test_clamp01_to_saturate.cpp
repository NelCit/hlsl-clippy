// End-to-end tests for the clamp01-to-saturate rule. Verifies that the
// rule fires on every literal-zero / literal-one variant and is silent for
// non-literal bounds or non-zero/non-one bounds.

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

[[nodiscard]] bool any_clamp01_diag(const std::vector<Diagnostic>& diagnostics) {
    return std::ranges::any_of(diagnostics,
                               [](const Diagnostic& d) { return d.code == "clamp01-to-saturate"; });
}

}  // namespace

TEST_CASE("clamp01-to-saturate fires on clamp(x, 0.0, 1.0)", "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return clamp(x, 0.0, 1.0); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate fires on integer-form clamp(x, 0, 1)", "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return clamp(x, 0, 1); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate fires on clamp(x, 0.0f, 1.0f)", "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return clamp(x, 0.0f, 1.0f); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate does not fire on clamp(x, a, b) with non-literal bounds",
          "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float a, float b) { return clamp(x, a, b); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK_FALSE(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate does not fire on clamp(x, 0.0, 2.0)", "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return clamp(x, 0.0, 2.0); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK_FALSE(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate does not fire on clamp(x, 0.5, 1.0)", "[rules][clamp01]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return clamp(x, 0.5, 1.0); }
)hlsl";
    const auto d = lint_buffer(hlsl, sources);
    CHECK_FALSE(any_clamp01_diag(d));
}

TEST_CASE("clamp01-to-saturate carries a machine-applicable fix", "[rules][clamp01][fix]") {
    SourceManager sources;
    const std::string hlsl = "float f(float x) { return clamp(x, 0.0, 1.0); }\n";
    const auto diagnostics = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diagnostics) {
        if (d.code == "clamp01-to-saturate") {
            hit = &d;
            break;
        }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "saturate(x)");
}

TEST_CASE("clamp01-to-saturate hits both fixture clamp_zero_one cases",
          "[rules][clamp01][fixture]") {
    std::filesystem::path fixture{std::string{shader_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "redundant.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto d = lint(sources, src, rules);

    int hits = 0;
    for (const auto& diag : d) {
        if (diag.code == "clamp01-to-saturate") {
            ++hits;
        }
    }
    // The fixture has two `clamp(x, 0.0, 1.0)` lines (lines 16 and 21).
    CHECK(hits == 2);
}
