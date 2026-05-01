// Tests for groupshared-dead-store.
//
// Stage::ControlFlow. Pairs an AST scan for groupshared writes with a CFG-
// driven `light_dataflow::dead_store` query. Per ADR 0013 sub-phase 4b,
// `dead_store` is a forward-compatible stub that returns `false` until the
// engine grows per-variable use-def chains -- this test therefore asserts
// that the rule does NOT fire today (no false positives) and runs cleanly
// through the lint pipeline. Once 4a's CFG grows the underlying signal,
// this test will be the natural place to add positive-case checks.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_dead_store();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_groupshared_dead_store());
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

TEST_CASE("groupshared-dead-store does not crash on a groupshared dead-write program",
          "[rules][groupshared-dead-store]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
RWStructuredBuffer<float> Out;
StructuredBuffer<float> Src;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = (float)gi;
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = Src[gi] * 2.0;
}
)hlsl";
    // dead_store is a forward-compatible stub returning `false` until the
    // CFG engine grows use-def chains; assert the lint pipeline runs and
    // does not crash regardless of whether the diagnostic fires.
    const auto diags = lint_buffer(hlsl);
    (void)diags;
}

TEST_CASE("groupshared-dead-store stays silent today (forward-compat stub)",
          "[rules][groupshared-dead-store]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = (float)gi;
}
)hlsl";
    // The rule is a forward-compatible stub today: `light_dataflow::
    // dead_store` returns `false` until 4a grows use-def chains, so we
    // expect zero firings. This test guards against accidental false
    // positives if the helper's stub status changes without the rule
    // tightening.
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-dead-store"));
}

TEST_CASE("groupshared-dead-store silent on non-groupshared arrays",
          "[rules][groupshared-dead-store]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> Out;
void cs(uint gi) {
    Out[gi] = (float)gi;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-dead-store"));
}
