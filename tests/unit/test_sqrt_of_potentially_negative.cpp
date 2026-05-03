// End-to-end tests for the sqrt-of-potentially-negative rule.

#include <memory>
#include <string>
#include <string_view>
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
    const auto src = sources.add_buffer("sqrt.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

[[nodiscard]] const Diagnostic* find_rule(const std::vector<Diagnostic>& diags,
                                          std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return &d;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("sqrt-of-potentially-negative fires on sqrt(a - b)",
          "[rules][sqrt-of-potentially-negative]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float dist(float r2, float h2) {
    return sqrt(r2 - h2);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "sqrt-of-potentially-negative"));
}

TEST_CASE("sqrt-of-potentially-negative does not fire on sqrt(x*x + y*y)",
          "[rules][sqrt-of-potentially-negative]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float hypot(float x, float y) { return sqrt(x*x + y*y); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "sqrt-of-potentially-negative");
}

TEST_CASE("sqrt-of-potentially-negative does not fire on sqrt(max(0.0, a-b))",
          "[rules][sqrt-of-potentially-negative]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float dist_safe(float r2, float h2) {
    return sqrt(max(0.0, r2 - h2));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "sqrt-of-potentially-negative");
}

TEST_CASE("sqrt-of-potentially-negative fix wraps the argument in max(0.0, ...)",
          "[rules][sqrt-of-potentially-negative][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float dist(float r2, float h2) {
    return sqrt(r2 - h2);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "sqrt-of-potentially-negative");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->fixes.size() == 1U);
    // The wrap is single-span and evaluates the inner expression once -- no
    // side-effect duplication, machine-applicable.
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "max(0.0, r2 - h2)");
}
