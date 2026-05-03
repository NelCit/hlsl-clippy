// End-to-end tests for the lerp-on-bool-cond rule.
// `lerp(a, b, (float)cond)` and `lerp(a, b, cond ? 1.0 : 0.0)` -> `cond ? b : a`.

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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
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

TEST_CASE("lerp-on-bool-cond fires on lerp(a, b, (float)cond)",
          "[rules][lerp-on-bool-cond]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, bool cond) { return lerp(a, b, (float)cond); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "lerp-on-bool-cond"));
}

TEST_CASE("lerp-on-bool-cond fires on lerp(a, b, cond ? 1.0 : 0.0)",
          "[rules][lerp-on-bool-cond]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, bool cond) { return lerp(a, b, cond ? 1.0 : 0.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "lerp-on-bool-cond"));
}

TEST_CASE("lerp-on-bool-cond fires on inverted ternary lerp(a, b, cond ? 0 : 1)",
          "[rules][lerp-on-bool-cond]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, bool cond) { return lerp(a, b, cond ? 0.0 : 1.0); }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "lerp-on-bool-cond"));
}

TEST_CASE("lerp-on-bool-cond does not fire on lerp(a, b, t)",
          "[rules][lerp-on-bool-cond]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, float t) { return lerp(a, b, t); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "lerp-on-bool-cond");
}

TEST_CASE("lerp-on-bool-cond does not fire on lerp(a, b, cond ? 0.5 : 0.25)",
          "[rules][lerp-on-bool-cond]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, bool cond) { return lerp(a, b, cond ? 0.5 : 0.25); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) CHECK(d.code != "lerp-on-bool-cond");
}

TEST_CASE("lerp-on-bool-cond fix uses cond ? b : a",
          "[rules][lerp-on-bool-cond][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float f(float a, float b, bool cond) { return lerp(a, b, (float)cond); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "lerp-on-bool-cond");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    // v1.2 (ADR 0019): both `a` and `b` are bare identifiers -- the purity
    // oracle classifies them as SideEffectFree, so the rewrite is
    // machine-applicable.
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "cond ? b : a");
}

TEST_CASE("lerp-on-bool-cond fix downgrades to suggestion when an operand is impure",
          "[rules][lerp-on-bool-cond][fix]") {
    // `g(x)` is an unknown call -- the purity oracle returns SideEffectful,
    // so the rewrite drops to suggestion-grade (machine_applicable = false).
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float g(float x);
float f(float x, float b, bool cond) { return lerp(g(x), b, (float)cond); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "lerp-on-bool-cond");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "cond ? b : g(x)");
}
