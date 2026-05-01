// End-to-end tests for the pow-to-mul rule.
// pow(x, 2.0) -> x * x; pow(x, 3.0) -> x * x * x; pow(x, 4.0) -> (x*x)*(x*x).

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

TEST_CASE("pow-to-mul fires on pow(x, 2.0)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 2.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-to-mul"));
}

TEST_CASE("pow-to-mul fires on pow(x, 3.0)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 3.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-to-mul"));
}

TEST_CASE("pow-to-mul fires on pow(x, 4.0)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 4.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-to-mul"));
}

TEST_CASE("pow-to-mul does not fire on pow(2.0, x)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(2.0, x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-to-mul");
}

TEST_CASE("pow-to-mul does not fire on pow(x, 5.0)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 5.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-to-mul");
}

TEST_CASE("pow-to-mul does not fire on pow(x, 2.5)", "[rules][pow-to-mul]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 2.5); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-to-mul");
}

TEST_CASE("pow-to-mul fix on identifier base is machine-applicable", "[rules][pow-to-mul][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 3.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "pow-to-mul");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "x * x * x");
}

TEST_CASE("pow-to-mul fix on complex base is suggestion-only", "[rules][pow-to-mul][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x, float y) { return pow(x + y, 2.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "pow-to-mul");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}

TEST_CASE("pow-to-mul fix for exponent 4 produces (x*x)*(x*x)", "[rules][pow-to-mul][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 4.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "pow-to-mul");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "(x * x) * (x * x)");
}
