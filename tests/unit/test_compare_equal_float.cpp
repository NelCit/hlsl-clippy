// End-to-end tests for the compare-equal-float rule.
// Detects exact `==` / `!=` against a floating-point literal operand.

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

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags,
                            std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

// ---- positive cases ----

TEST_CASE("compare-equal-float fires on x == 0.0", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

TEST_CASE("compare-equal-float fires on x != 1.0f", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x != 1.0f; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

TEST_CASE("compare-equal-float fires when literal is on the left", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return 0.5 == x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

// ---- negative cases ----

TEST_CASE("compare-equal-float does not fire on integer literal compare",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(int x) { return x == 0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

TEST_CASE("compare-equal-float does not fire on `<` ordering compare",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x < 0.5; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

TEST_CASE("compare-equal-float does not fire when neither operand is a float literal",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float a, float b) { return a == b; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

// ---- fix is suggestion-only ----

TEST_CASE("compare-equal-float fix is suggestion-only", "[rules][compare-equal-float][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "compare-equal-float") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}
