// End-to-end tests for the manual-smoothstep rule.
// Hand-rolled cubic Hermite (saturate + n*n*(3-2*n)) should suggest smoothstep().

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

TEST_CASE("manual-smoothstep fires on canonical two-statement form", "[rules][manual-smoothstep]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) {
    float n = saturate((t - a) / (b - a));
    return n * n * (3.0 - 2.0 * n);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-smoothstep"));
}

TEST_CASE("manual-smoothstep fires with integer literal constants", "[rules][manual-smoothstep]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) {
    float n = saturate((t - a) / (b - a));
    return n * n * (3 - 2 * n);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-smoothstep"));
}

TEST_CASE("manual-smoothstep does not fire when constants are wrong (2-3*n form)",
          "[rules][manual-smoothstep]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) {
    float n = saturate((t - a) / (b - a));
    return n * n * (2.0 - 3.0 * n);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-smoothstep");
}

TEST_CASE("manual-smoothstep does not fire on a simple lerp (no saturate normalize)",
          "[rules][manual-smoothstep]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) {
    float n = (t - a) / (b - a);
    return n * n * (3.0 - 2.0 * n);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-smoothstep");
}

TEST_CASE("manual-smoothstep fix is suggestion-only", "[rules][manual-smoothstep][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) {
    float n = saturate((t - a) / (b - a));
    return n * n * (3.0 - 2.0 * n);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "manual-smoothstep") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    // Fix description should mention smoothstep.
    CHECK(hit->fixes[0].description.find("smoothstep") != std::string::npos);
}

TEST_CASE("manual-smoothstep fires on math.hlsl fixture", "[rules][manual-smoothstep][fixture]") {
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
        if (d.code == "manual-smoothstep") ++count;
    }
    CHECK(count >= 1U);
}
