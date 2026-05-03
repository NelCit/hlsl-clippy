// End-to-end tests for the acos-without-saturate rule.

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
    const auto src = sources.add_buffer("acos.hlsl", hlsl);
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

TEST_CASE("acos-without-saturate fires on acos(dot(...))", "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(dot(normalize(a), normalize(b)));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "acos-without-saturate"));
}

TEST_CASE("acos-without-saturate does not fire on acos(saturate(dot(...)))",
          "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(saturate(dot(normalize(a), normalize(b))));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "acos-without-saturate");
}

TEST_CASE("acos-without-saturate does not fire on acos(clamp(dot(...), -1, 1))",
          "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(clamp(dot(a, b), -1.0, 1.0));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "acos-without-saturate");
}

TEST_CASE("acos-without-saturate fires on asin(dot(...)) too", "[rules][acos-without-saturate]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float arc(float3 a, float3 b) {
    return asin(dot(a, b));
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "acos-without-saturate"));
}

TEST_CASE("acos-without-saturate fix wraps the argument in clamp(..., -1.0, 1.0)",
          "[rules][acos-without-saturate][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float angle(float3 a, float3 b) {
    return acos(dot(a, b));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "acos-without-saturate");
    REQUIRE(hit != nullptr);
    REQUIRE(hit->fixes.size() == 1U);
    // Wrapping the entire arg expression in `clamp(...)` evaluates the inner
    // expression exactly once -- no side-effect duplication, machine-applicable.
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "clamp(dot(a, b), -1.0, 1.0)");
}
