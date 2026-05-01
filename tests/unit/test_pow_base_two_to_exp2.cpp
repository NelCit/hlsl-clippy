// End-to-end tests for the pow-base-two-to-exp2 rule.
// pow(2.0, x) -> exp2(x), machine-applicable.

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

TEST_CASE("pow-base-two-to-exp2 fires on pow(2.0, x)", "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(2.0, x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-base-two-to-exp2"));
}

TEST_CASE("pow-base-two-to-exp2 fires on pow(2, x) (integer literal)",
          "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(2, x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-base-two-to-exp2"));
}

TEST_CASE("pow-base-two-to-exp2 fires on pow(2.0f, -t * 8.0)",
          "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float t) { return pow(2.0f, -t * 8.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "pow-base-two-to-exp2"));
}

TEST_CASE("pow-base-two-to-exp2 does not fire on pow(x, 2.0)",
          "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(x, 2.0); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-base-two-to-exp2");
}

TEST_CASE("pow-base-two-to-exp2 does not fire on pow(3.0, x)",
          "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(3.0, x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-base-two-to-exp2");
}

TEST_CASE("pow-base-two-to-exp2 does not fire on plain exp2 call",
          "[rules][pow-base-two-to-exp2]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return exp2(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "pow-base-two-to-exp2");
}

TEST_CASE("pow-base-two-to-exp2 fix replaces with exp2(x)",
          "[rules][pow-base-two-to-exp2][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return pow(2.0, x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "pow-base-two-to-exp2");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "exp2(x)");
}
