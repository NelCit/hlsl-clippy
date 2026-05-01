// End-to-end tests for the manual-mad-decomposition rule.
// (a*b)+c or c+(a*b) -> mad(a, b, c); suggestion-only.

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

// ---- positive cases ----

TEST_CASE("manual-mad-decomposition fires on (a * b) + c",
          "[rules][manual-mad-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float c) { return (a * b) + c; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-mad-decomposition"));
}

TEST_CASE("manual-mad-decomposition fires on c + (a * b)",
          "[rules][manual-mad-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float c) { return c + (a * b); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-mad-decomposition"));
}

TEST_CASE("manual-mad-decomposition fires on vector operands",
          "[rules][manual-mad-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b, float3 c) { return (a * b) + c; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-mad-decomposition"));
}

// ---- negative cases ----

TEST_CASE("manual-mad-decomposition does not fire on plain a * b + c (no parens)",
          "[rules][manual-mad-decomposition]") {
    // Without parens the rule scopes itself out -- the developer has not
    // explicitly grouped the multiply, which is the signal we look for.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float c) { return a * b + c; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-mad-decomposition");
}

TEST_CASE("manual-mad-decomposition does not fire on a + b (no multiply)",
          "[rules][manual-mad-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b) { return a + b; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-mad-decomposition");
}

TEST_CASE("manual-mad-decomposition does not fire on the mad intrinsic itself",
          "[rules][manual-mad-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float c) { return mad(a, b, c); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-mad-decomposition");
}

TEST_CASE("manual-mad-decomposition does not fire on (a * b) + (d * e)",
          "[rules][manual-mad-decomposition]") {
    // Both operands are parenthesised multiplies -- ambiguous which is the
    // multiply and which is the addend, so the rule stays quiet.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float d, float e) { return (a * b) + (d * e); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "manual-mad-decomposition");
}

// ---- fix applicability ----

TEST_CASE("manual-mad-decomposition fix is suggestion-only and proposes mad(a, b, c)",
          "[rules][manual-mad-decomposition][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float c) { return (a * b) + c; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "manual-mad-decomposition") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "mad(a, b, c)");
}
