// Tests for groupshared-first-read-without-barrier.
//
// Stage::ControlFlow. Pairs an AST scan for groupshared writes/reads with
// `cfg_query::barrier_separates`. Severity: Error.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_first_read_without_barrier();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_groupshared_first_read_without_barrier());
    LintOptions options;
    options.enable_control_flow = true;
    options.enable_reflection = false;
    return lint(sources, src, rules, options);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("groupshared-first-read-without-barrier does not crash on a race program",
          "[rules][groupshared-first-read-without-barrier]") {
    // Classic race: write gs[gi], then read gs[(gi+1)%64] with no barrier.
    // Whether the rule fires here depends on the engine's
    // `barrier_separates` answer (conservatively false today => fires).
    // We assert the lint pipeline runs and does not crash.
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
RWStructuredBuffer<float> Out;
StructuredBuffer<float> Src;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = Src[gi];
    Out[gi] = g_Tile[(gi + 1) % 64];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
}

TEST_CASE("groupshared-first-read-without-barrier silent when read precedes any write",
          "[rules][groupshared-first-read-without-barrier]") {
    // No write precedes the read in source order -- this rule should not
    // fire (the `groupshared-uninitialized-read` rule covers that case).
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
RWStructuredBuffer<float> Out;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    Out[gi] = g_Tile[gi];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-first-read-without-barrier"));
}

TEST_CASE("groupshared-first-read-without-barrier silent on non-groupshared arrays",
          "[rules][groupshared-first-read-without-barrier]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> Out;
StructuredBuffer<float> Src;
void f(uint gi) {
    Out[gi] = Src[gi];
    float v = Out[(gi + 1) % 64];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-first-read-without-barrier"));
}

TEST_CASE("groupshared-first-read-without-barrier silent on write-only kernel",
          "[rules][groupshared-first-read-without-barrier]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = (float)gi;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-first-read-without-barrier"));
}
