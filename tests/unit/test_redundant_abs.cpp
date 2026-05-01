// End-to-end tests for the redundant-abs rule.
// abs(saturate(x)), abs(x*x), abs(dot(v,v)) are all redundant; fix is
// machine-applicable and drops the abs wrapper.

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

TEST_CASE("redundant-abs fires on abs(saturate(x))", "[rules][redundant-abs]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return abs(saturate(x)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-abs"));
}

TEST_CASE("redundant-abs fires on abs(x * x)", "[rules][redundant-abs]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return abs(x * x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-abs"));
}

TEST_CASE("redundant-abs fires on abs(v.x * v.x)", "[rules][redundant-abs]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return abs(v.x * v.x); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-abs"));
}

TEST_CASE("redundant-abs fires on abs(dot(v, v))", "[rules][redundant-abs]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return abs(dot(v, v)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-abs"));
}

// ---- negative cases ----

TEST_CASE("redundant-abs does not fire on abs(x)", "[rules][redundant-abs]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return abs(x); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-abs");
}

TEST_CASE("redundant-abs does not fire on abs(a * b)", "[rules][redundant-abs]") {
    // Different operands -- the product can be negative; abs is not redundant.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b) { return abs(a * b); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-abs");
}

TEST_CASE("redundant-abs does not fire on abs(dot(a, b))", "[rules][redundant-abs]") {
    // Different vectors -- dot can be negative.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 a, float3 b) { return abs(dot(a, b)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-abs");
}

// ---- fix applicability ----

TEST_CASE("redundant-abs fix on abs(saturate(x)) drops the abs",
          "[rules][redundant-abs][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float x) { return abs(saturate(x)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "redundant-abs") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "saturate(x)");
}

TEST_CASE("redundant-abs fix on abs(dot(v, v)) drops the abs",
          "[rules][redundant-abs][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float3 v) { return abs(dot(v, v)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "redundant-abs") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "dot(v, v)");
}
