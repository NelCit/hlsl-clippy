// End-to-end tests for the mul-identity rule.
// x * 1 -> x, x + 0 -> x (machine-applicable), x * 0 -> suggestion-only.

#include <filesystem>
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

// ---- mul-identity fires ----

TEST_CASE("mul-identity fires on x * 1.0", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 1.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

TEST_CASE("mul-identity fires on 1.0 * x", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 1.0 * x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

TEST_CASE("mul-identity fires on x + 0.0", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x + 0.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

TEST_CASE("mul-identity fires on 0.0 + x", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 0.0 + x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

TEST_CASE("mul-identity fires on x * 0.0 (suggestion-only)", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 0.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

TEST_CASE("mul-identity fires on 0.0 * x (suggestion-only)", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return 0.0 * x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "mul-identity"));
}

// ---- does not fire ----

TEST_CASE("mul-identity does not fire on x * 1.001", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return v * 1.001; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "mul-identity");
}

TEST_CASE("mul-identity does not fire on x * 2.0", "[rules][mul-identity]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 2.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "mul-identity");
}

TEST_CASE("mul-identity does not fire on x - 0.0", "[rules][mul-identity]") {
    // Subtraction of zero is NOT the same pattern (x - 0 = x but the rule
    // only handles + and *).
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x - 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "mul-identity");
}

// ---- fix applicability ----

TEST_CASE("mul-identity x*1 fix is machine-applicable", "[rules][mul-identity][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 1.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "mul-identity") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "x");
}

TEST_CASE("mul-identity x+0 fix is machine-applicable", "[rules][mul-identity][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x + 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "mul-identity") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
}

TEST_CASE("mul-identity x*0 fix is suggestion-only", "[rules][mul-identity][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x * 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "mul-identity") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}

TEST_CASE("mul-identity fires on the phase2 math fixture", "[rules][mul-identity][fixture]") {
    std::filesystem::path fixture{std::string{shader_clippy::test::k_fixtures_dir}};
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
        if (d.code == "mul-identity") ++count;
    }
    // math.hlsl has 3 mul-identity hits: v*1.0, a+0.0, b*0.0
    CHECK(count >= 3U);
}

TEST_CASE("mul-identity does not fire on negative_lookalikes fixture",
          "[rules][mul-identity][fixture]") {
    std::filesystem::path fixture{std::string{shader_clippy::test::k_fixtures_dir}};
    fixture /= "phase2";
    fixture /= "negative_lookalikes.hlsl";
    REQUIRE(std::filesystem::exists(fixture));

    SourceManager sources;
    const auto src = sources.add_file(fixture);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    const auto diags = lint(sources, src, rules);

    for (const auto& d : diags) CHECK(d.code != "mul-identity");
}
