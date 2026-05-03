// End-to-end tests for the comparison-with-nan-literal rule.
// Detects comparisons where one operand textually evaluates to NaN.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

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

TEST_CASE("comparison-with-nan-literal fires on x == (0.0/0.0)",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == (0.0 / 0.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "comparison-with-nan-literal"));
}

TEST_CASE("comparison-with-nan-literal fires on x < (0.0/0.0)",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x < (0.0 / 0.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "comparison-with-nan-literal"));
}

TEST_CASE("comparison-with-nan-literal fires on x != NAN",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
static const float NAN = 0.0 / 0.0;
bool f(float x) { return x != NAN; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "comparison-with-nan-literal"));
}

TEST_CASE("comparison-with-nan-literal fires when NaN is on the left side",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return (0.0f/0.0f) >= x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "comparison-with-nan-literal"));
}

// ---- negative cases ----

TEST_CASE("comparison-with-nan-literal does not fire on isnan(x)",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return isnan(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "comparison-with-nan-literal");
    }
}

TEST_CASE("comparison-with-nan-literal does not fire on x == 0.5",
          "[rules][comparison-with-nan-literal]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.5; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "comparison-with-nan-literal");
    }
}

TEST_CASE("comparison-with-nan-literal does not fire on a non-comparison op",
          "[rules][comparison-with-nan-literal]") {
    // Arithmetic with the NaN expression is not flagged by this rule.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return x + (0.0 / 0.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "comparison-with-nan-literal");
    }
}

// ---- fix is suggestion-only ----

TEST_CASE("comparison-with-nan-literal fix is suggestion-only",
          "[rules][comparison-with-nan-literal][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == (0.0 / 0.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "comparison-with-nan-literal") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}
