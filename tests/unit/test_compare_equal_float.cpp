// End-to-end tests for the compare-equal-float rule.
// Detects exact `==` / `!=` against a floating-point literal operand.

#include <filesystem>
#include <string>
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

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags,
                            std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) return true;
    }
    return false;
}

}  // namespace

// ---- positive cases ----

TEST_CASE("compare-equal-float fires on x == 0.0", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.0; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

TEST_CASE("compare-equal-float fires on x != 1.0f", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x != 1.0f; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

TEST_CASE("compare-equal-float fires when literal is on the left", "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return 0.5 == x; }
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "compare-equal-float"));
}

// ---- negative cases ----

TEST_CASE("compare-equal-float does not fire on integer literal compare",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(int x) { return x == 0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

TEST_CASE("compare-equal-float does not fire on `<` ordering compare",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x < 0.5; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

TEST_CASE("compare-equal-float does not fire when neither operand is a float literal",
          "[rules][compare-equal-float]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float a, float b) { return a == b; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compare-equal-float");
    }
}

// ---- fix grade -- v1.2 (ADR 0019) machine-applicable when operands are pure ----

TEST_CASE("compare-equal-float fix is machine-applicable on pure operands",
          "[rules][compare-equal-float][fix]") {
    // v1.2: both operands are pure (identifier `x` + numeric literal `0.0`).
    // The purity oracle clears them, so the rewrite is machine-applicable.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.0; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "compare-equal-float") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    // Default `compare_epsilon` is 1e-4f; the `==` flavour rewrites to `<`.
    CHECK(hit->fixes[0].edits[0].replacement == "abs((x) - (0.0)) < 0.0001f");
}

TEST_CASE("compare-equal-float `!=` rewrites to `>= epsilon`",
          "[rules][compare-equal-float][fix]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x != 1.0f; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "compare-equal-float") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    CHECK(hit->fixes[0].edits[0].replacement == "abs((x) - (1.0f)) >= 0.0001f");
}

TEST_CASE("compare-equal-float fix downgrades to suggestion when an operand is impure",
          "[rules][compare-equal-float][fix]") {
    // `g(x)` is an unknown call -- the purity oracle returns SideEffectful,
    // so the rewrite drops to suggestion-grade (machine_applicable = false).
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float g(float x);
bool f(float x) { return g(x) == 0.5; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "compare-equal-float") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK_FALSE(hit->fixes[0].machine_applicable);
}

TEST_CASE("compare-equal-float honours `[float] compare-epsilon` from config",
          "[rules][compare-equal-float][fix][config]") {
    // When a Config is wired through the lint orchestrator, the rule pulls
    // the project-tuned epsilon (here `0.05`) into the rewrite literal.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
bool f(float x) { return x == 0.0; }
)hlsl";
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    Config cfg{};
    cfg.compare_epsilon_value = 0.05F;
    const auto diags = lint(sources, src, rules, cfg, std::filesystem::path{"synthetic.hlsl"});
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "compare-equal-float") { hit = &d; break; }
    }
    REQUIRE(hit != nullptr);
    REQUIRE_FALSE(hit->fixes.empty());
    CHECK(hit->fixes[0].machine_applicable);
    REQUIRE(hit->fixes[0].edits.size() == 1U);
    // 0.05 renders through the precision-7 fallback path.
    CHECK(hit->fixes[0].edits[0].replacement == "abs((x) - (0.0)) < 0.05f");
}
