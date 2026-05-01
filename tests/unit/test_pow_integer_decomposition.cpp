// End-to-end tests for the pow-integer-decomposition rule.
// pow(x, N.0) for N >= 5: suggestion-only (no textual rewrite).

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

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("pow-integer-decomposition fires on pow(x, 5.0)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 5.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-integer-decomposition"));
}

TEST_CASE("pow-integer-decomposition fires on pow(x, 8.0)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 8.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-integer-decomposition"));
}

TEST_CASE("pow-integer-decomposition does not fire on pow(x, 4.0)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 4.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-integer-decomposition");
}

TEST_CASE("pow-integer-decomposition does not fire on pow(x, 3.0)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 3.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-integer-decomposition");
}

TEST_CASE("pow-integer-decomposition does not fire on pow(2.0, x)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(2.0, x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-integer-decomposition");
}

TEST_CASE("pow-integer-decomposition does not fire on pow(x, 5.5)",
          "[rules][pow-integer-decomposition]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 5.5); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-integer-decomposition");
}

TEST_CASE("pow-integer-decomposition fix is suggestion-only with no edits",
          "[rules][pow-integer-decomposition][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 7.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "pow-integer-decomposition");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    // Suggestion-only: no textual edits emitted.
    CHECK(hit->fixes[0].edits.empty());
}
