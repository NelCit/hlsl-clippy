// End-to-end tests for the manual-step rule.
// x > threshold ? 1.0 : 0.0 should be replaced with step(threshold, x).

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

TEST_CASE("manual-step fires on x > threshold ? 1.0 : 0.0", "[rules][manual-step]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float threshold) {
    return x > threshold ? 1.0 : 0.0;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-step"));
}

TEST_CASE("manual-step fires on x > a ? 1 : 0 (integer literals)", "[rules][manual-step]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float a) { return x > a ? 1 : 0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-step"));
}

TEST_CASE("manual-step does not fire on x > a ? 2.0 : -1.0 (non-step values)",
          "[rules][manual-step]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float a) { return x > a ? 2.0 : -1.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-step");
}

TEST_CASE("manual-step does not fire on x >= a ? 1.0 : 0.0 (already >= semantics)",
          "[rules][manual-step]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float a) { return x >= a ? 1.0 : 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-step");
}

TEST_CASE("manual-step does not fire on swapped arms x > a ? 0.0 : 1.0",
          "[rules][manual-step]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float a) { return x > a ? 0.0 : 1.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-step");
}

TEST_CASE("manual-step fix is suggestion-only", "[rules][manual-step][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float threshold) { return x > threshold ? 1.0 : 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "manual-step") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    CHECK(hit->fixes[0].edits[0].replacement.find("step") != std::string::npos);
}

TEST_CASE("manual-step fires on math.hlsl fixture", "[rules][manual-step][fixture]") {
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
        if (d.code == "manual-step") ++count;
    }
    CHECK(count >= 1U);
}

TEST_CASE("manual-step does not fire on negative_lookalikes fixture",
          "[rules][manual-step][fixture]") {
    std::filesystem::path fixture{std::string{hlsl_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "negative_lookalikes.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    for (const auto& d : diags) CHECK(d.code != "manual-step");
}
