// End-to-end tests for the manual-wave-reduction-pattern rule (ADR 0011
// §Phase 4 pack C, ADR 0013 sub-phase 4c). The rule fires on hand-rolled
// reductions that should collapse to a `WaveActive*` intrinsic.
//
// Coverage:
//   * Tree-reduction loop with halving stride + GroupMemoryBarrier => fires.
//   * Atomic-counter loop reduction (InterlockedAdd in a for body) => fires.
//   * WaveReadLaneAt-XOR butterfly ladder => fires.
//   * Plain non-reduction loop => does not fire.
//   * Tree-reduction without barrier => does not fire (defensive).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_manual_wave_reduction_pattern();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_manual_wave_reduction_pattern());
    return lint(sources, src, rules);
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

TEST_CASE("manual-wave-reduction-pattern fires on tree-reduction with halving stride and barrier",
          "[rules][manual-wave-reduction-pattern]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float g_Reduce[32];

[numthreads(32, 1, 1)]
void cs_manual_sum(uint gi : SV_GroupIndex)
{
    g_Reduce[gi] = 1.0;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 16; stride > 0; stride >>= 1) {
        if (gi < stride) {
            g_Reduce[gi] += g_Reduce[gi + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-wave-reduction-pattern"));
}

TEST_CASE("manual-wave-reduction-pattern fires on atomic-counter loop reduction",
          "[rules][manual-wave-reduction-pattern]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared uint g_Sum;
RWByteAddressBuffer Out;

[numthreads(32, 1, 1)]
void cs_atomic_sum(uint gi : SV_GroupIndex)
{
    for (uint i = 0; i < 32; ++i) {
        InterlockedAdd(g_Sum, i);
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-wave-reduction-pattern"));
}

TEST_CASE("manual-wave-reduction-pattern fires on WaveReadLaneAt XOR butterfly",
          "[rules][manual-wave-reduction-pattern]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(32, 1, 1)]
void cs_butterfly(uint gi : SV_GroupIndex)
{
    float v = (float)gi;
    for (uint k = 1; k < 32; k <<= 1) {
        v += WaveReadLaneAt(v, gi ^ k);
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "manual-wave-reduction-pattern"));
}

TEST_CASE("manual-wave-reduction-pattern does not fire on a plain compute loop",
          "[rules][manual-wave-reduction-pattern]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(32, 1, 1)]
void cs_plain(uint gi : SV_GroupIndex)
{
    float v = 0.0;
    for (uint i = 0; i < 4; ++i) {
        v += (float)i;
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "manual-wave-reduction-pattern");
    }
}

TEST_CASE("manual-wave-reduction-pattern does not fire on halving loop without barrier",
          "[rules][manual-wave-reduction-pattern]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(32, 1, 1)]
void cs_halve_only(uint gi : SV_GroupIndex)
{
    uint x = gi;
    for (uint stride = 16; stride > 0; stride >>= 1) {
        x = x ^ stride;
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "manual-wave-reduction-pattern");
    }
}
