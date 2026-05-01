// End-to-end tests for the firstbit-vs-log2-trick rule.
// (uint)log2(x) -> firstbithigh(x); suggestion-only.

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

TEST_CASE("firstbit-vs-log2-trick fires on (uint)log2((float)x)",
          "[rules][firstbit-vs-log2-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint highbit(uint x) { return (uint)log2((float)x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "firstbit-vs-log2-trick"));
}

TEST_CASE("firstbit-vs-log2-trick fires on (uint)log2(x)", "[rules][firstbit-vs-log2-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint highbit(float x) { return (uint)log2(x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "firstbit-vs-log2-trick"));
}

// ---- negative cases ----

TEST_CASE("firstbit-vs-log2-trick does not fire on a plain log2 (no uint cast)",
          "[rules][firstbit-vs-log2-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return log2(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "firstbit-vs-log2-trick");
}

TEST_CASE("firstbit-vs-log2-trick does not fire on (int)log2(x)",
          "[rules][firstbit-vs-log2-trick]") {
    // Only uint-family casts trigger the rule; int casts are typically a
    // different intent (signed result, possibly negative for x < 1).
    SourceManager sources;
    const std::string hlsl = R"hlsl(
int f(float x) { return (int)log2(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "firstbit-vs-log2-trick");
}

TEST_CASE("firstbit-vs-log2-trick does not fire on (uint)log(x)",
          "[rules][firstbit-vs-log2-trick]") {
    // Different transcendental -- not the trick.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(float x) { return (uint)log(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "firstbit-vs-log2-trick");
}

TEST_CASE("firstbit-vs-log2-trick does not fire on firstbithigh itself",
          "[rules][firstbit-vs-log2-trick]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint f(uint x) { return firstbithigh(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "firstbit-vs-log2-trick");
}

// ---- fix applicability ----

TEST_CASE("firstbit-vs-log2-trick fix is suggestion-only",
          "[rules][firstbit-vs-log2-trick][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
uint highbit(uint x) { return (uint)log2((float)x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "firstbit-vs-log2-trick") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}
