// End-to-end tests for the redundant-normalize rule.
// normalize(normalize(x)) -> normalize(x), machine-applicable.

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

TEST_CASE("redundant-normalize fires on normalize(normalize(v))", "[rules][redundant-normalize]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return normalize(normalize(v)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-normalize"));
}

TEST_CASE("redundant-normalize fires on normalize(normalize(a + b))",
          "[rules][redundant-normalize]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return normalize(normalize(a + b)); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "redundant-normalize"));
}

TEST_CASE("redundant-normalize does not fire on a single normalize",
          "[rules][redundant-normalize]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return normalize(v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-normalize");
}

TEST_CASE("redundant-normalize does not fire on normalize(saturate(v))",
          "[rules][redundant-normalize]") {
    // Different inner function -- not the redundant pattern.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return normalize(saturate(v)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-normalize");
}

TEST_CASE("redundant-normalize does not fire on normalize(cross(a, b))",
          "[rules][redundant-normalize]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 a, float3 b) { return normalize(cross(a, b)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "redundant-normalize");
}

TEST_CASE("redundant-normalize fix is machine-applicable and replaces with inner call",
          "[rules][redundant-normalize][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return normalize(normalize(v)); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "redundant-normalize") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "normalize(v)");
}
