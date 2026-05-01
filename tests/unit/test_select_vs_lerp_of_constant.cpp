// End-to-end tests for the select-vs-lerp-of-constant rule.
// `lerp(K1, K2, t)` -> `mad(t, K2 - K1, K1)` for compile-portable folding.

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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
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

TEST_CASE("select-vs-lerp-of-constant fires on lerp(0.0, 1.0, t)",
          "[rules][select-vs-lerp-of-constant]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float t) { return lerp(0.0, 1.0, t); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "select-vs-lerp-of-constant"));
}

TEST_CASE("select-vs-lerp-of-constant fires on lerp(2.5, 7.0, t)",
          "[rules][select-vs-lerp-of-constant]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float t) { return lerp(2.5, 7.0, t); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "select-vs-lerp-of-constant"));
}

TEST_CASE("select-vs-lerp-of-constant does not fire on lerp(a, b, t)",
          "[rules][select-vs-lerp-of-constant]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) { return lerp(a, b, t); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "select-vs-lerp-of-constant");
}

TEST_CASE("select-vs-lerp-of-constant does not fire on lerp(K1, b, t)",
          "[rules][select-vs-lerp-of-constant]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float b, float t) { return lerp(0.0, b, t); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "select-vs-lerp-of-constant");
}

TEST_CASE("select-vs-lerp-of-constant fix is mad(t, K2 - K1, K1)",
          "[rules][select-vs-lerp-of-constant][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float t) { return lerp(0.0, 1.0, t); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "select-vs-lerp-of-constant");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "mad(t, 1.0 - 0.0, 0.0)");
}
