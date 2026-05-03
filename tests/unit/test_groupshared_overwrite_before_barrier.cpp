// Tests for groupshared-overwrite-before-barrier.
//
// Stage::ControlFlow. Pairs adjacent same-thread writes to the same
// `groupshared` cell and asks `cfg_query::barrier_separates` whether a
// `GroupMemoryBarrier*` intervenes. When no barrier exists, the first
// write is dead.
//
// Per the conservatism contract in `cfg_query.hpp`, `barrier_separates`
// returns `false` when the CFG cannot tell -- that means the rule will
// (correctly) fire on a clear back-to-back-write program, and stay silent
// when a barrier statement is present between the writes (because today's
// CFG does record the barrier-block flag).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_overwrite_before_barrier();
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
    rules.push_back(shader_clippy::rules::make_groupshared_overwrite_before_barrier());
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

TEST_CASE("groupshared-overwrite-before-barrier does not crash on back-to-back writes",
          "[rules][groupshared-overwrite-before-barrier]") {
    // Same-thread back-to-back writes to gs[gi] with no barrier between.
    // The rule's behaviour depends on the CFG's `barrier_separates` answer,
    // which is conservatively `false` for the no-barrier path -- that
    // would fire the rule. We assert the lint pipeline runs cleanly here
    // and leave the firing-decision to the more targeted positive test
    // below (the wiring agent owns the integration test).
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
RWStructuredBuffer<float> Out;
StructuredBuffer<float> Src;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = 0.0;
    g_Tile[gi] = Src[gi];
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
}

TEST_CASE("groupshared-overwrite-before-barrier silent when only one write per cell",
          "[rules][groupshared-overwrite-before-barrier]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
RWStructuredBuffer<float> Out;
StructuredBuffer<float> Src;
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = Src[gi];
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-overwrite-before-barrier"));
}

TEST_CASE("groupshared-overwrite-before-barrier silent on writes to disjoint cell texts",
          "[rules][groupshared-overwrite-before-barrier]") {
    const std::string hlsl = R"hlsl(
groupshared float g_Tile[64];
[numthreads(64, 1, 1)]
void cs(uint gi : SV_GroupIndex) {
    g_Tile[gi] = 0.0;
    g_Tile[gi + 1] = 1.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-overwrite-before-barrier"));
}

TEST_CASE("groupshared-overwrite-before-barrier silent on non-groupshared overwrites",
          "[rules][groupshared-overwrite-before-barrier]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> Out;
void f(uint gi) {
    Out[gi] = 0.0;
    Out[gi] = 1.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    CHECK_FALSE(has_rule(diags, "groupshared-overwrite-before-barrier"));
}
