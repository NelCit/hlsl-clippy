// End-to-end tests for the div-without-epsilon rule.

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/config.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Config;
using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("div.hlsl", hlsl);
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

}  // namespace

TEST_CASE("div-without-epsilon fires on x / length(...)", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 unit_dir(float3 v) {
    return v / length(v);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "div-without-epsilon"));
}

TEST_CASE("div-without-epsilon fires on x / dot(...)", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ratio(float3 a, float3 b) {
    return 1.0 / dot(a, b);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "div-without-epsilon"));
}

TEST_CASE("div-without-epsilon does not fire on guarded divisor", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float ratio(float3 a, float3 b) {
    return 1.0 / max(1e-6, dot(a, b));
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "div-without-epsilon");
}

TEST_CASE("div-without-epsilon does not fire on x / 2.0", "[rules][div-without-epsilon]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float half_of(float x) { return x / 2.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags)
        CHECK(d.code != "div-without-epsilon");
}

// v1.2 (ADR 0019): machine-applicable rewrite wraps the divisor in max(eps, ...).

namespace {
[[nodiscard]] const shader_clippy::Diagnostic* find_rule(
    const std::vector<shader_clippy::Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return &d;
    }
    return nullptr;
}
}  // namespace

TEST_CASE("div-without-epsilon machine-applicable on pure divisor",
          "[rules][div-without-epsilon][fix]") {
    // `length(v)` -> the only argument is a bare identifier; the purity
    // oracle clears it, so the rewrite is machine-applicable.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 unit_dir(float3 v) { return v / length(v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "div-without-epsilon");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "max(1e-06f, length(v))");
}

TEST_CASE("div-without-epsilon downgrades to suggestion when divisor is impure",
          "[rules][div-without-epsilon][fix]") {
    // `dot(g(v), v)` -> `g(v)` is an unknown call, so the divisor is
    // SideEffectful under the purity oracle and the rewrite drops to
    // suggestion-grade.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 g(float3 x);
float ratio(float3 v) { return 1.0 / dot(g(v), v); }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const auto* hit = find_rule(diags, "div-without-epsilon");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}

TEST_CASE("div-without-epsilon honours `[float] div-epsilon` from config",
          "[rules][div-without-epsilon][fix][config]") {
    // When a Config is wired through the lint orchestrator, the rule pulls
    // the project-tuned epsilon (here `0.001`) into the `max(...)` literal.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float3 unit_dir(float3 v) { return v / length(v); }
)hlsl";
    const auto src = sources.add_buffer("div.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    Config cfg{};
    cfg.div_epsilon_value = 0.001F;
    const auto diags = lint(sources, src, rules, cfg, std::filesystem::path{"div.hlsl"});
    const auto* hit = find_rule(diags, "div-without-epsilon");
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "max(0.001f, length(v))");
}
