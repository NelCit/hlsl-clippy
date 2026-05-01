// End-to-end tests for the length-comparison rule.
// length(v) <op> r -> dot(v, v) <op> (r) * (r), suggestion-only.

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

TEST_CASE("length-comparison fires on length(v) < r", "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v, float r) { return length(v) < r; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "length-comparison"));
}

TEST_CASE("length-comparison fires on length(v) > r", "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v, float r) { return length(v) > r; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "length-comparison"));
}

TEST_CASE("length-comparison fires on length(v) <= 1.0", "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v) { return length(v) <= 1.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "length-comparison"));
}

TEST_CASE("length-comparison fires on r > length(v) (right-hand-side length)",
          "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v, float r) { return r > length(v); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "length-comparison"));
}

TEST_CASE("length-comparison does not fire on length(v) == r",
          "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v, float r) { return length(v) == r; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-comparison");
}

TEST_CASE("length-comparison does not fire on a < b (no length call)",
          "[rules][length-comparison]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float a, float b) { return a < b; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-comparison");
}

TEST_CASE("length-comparison fix is suggestion-only", "[rules][length-comparison][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float3 v, float r) { return length(v) < r; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "length-comparison");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement.find("dot(v, v)") != std::string::npos);
}
