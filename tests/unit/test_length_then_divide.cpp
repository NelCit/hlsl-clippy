// End-to-end tests for the length-then-divide rule.
// v / length(v) -> normalize(v), machine-applicable for simple identifier v.

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

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("length-then-divide fires on v / length(v)", "[rules][length-then-divide]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return v / length(v); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "length-then-divide"));
}

TEST_CASE("length-then-divide does not fire on v / length(w) (mismatched arg)",
          "[rules][length-then-divide]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v, float3 w) { return v / length(w); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-then-divide");
}

TEST_CASE("length-then-divide does not fire on v / 2.0 (no length call)",
          "[rules][length-then-divide]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return v / 2.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-then-divide");
}

TEST_CASE("length-then-divide does not fire on plain normalize(v)",
          "[rules][length-then-divide]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return normalize(v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-then-divide");
}

TEST_CASE("length-then-divide does not fire on length(v) / v (operand order matters)",
          "[rules][length-then-divide]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return length(v) / v; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "length-then-divide");
}

TEST_CASE("length-then-divide fix replaces with normalize(v)",
          "[rules][length-then-divide][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 f(float3 v) { return v / length(v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "length-then-divide");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "normalize(v)");
}
