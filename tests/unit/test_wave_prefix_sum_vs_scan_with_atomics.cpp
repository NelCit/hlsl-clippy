// End-to-end tests for the wave-prefix-sum-vs-scan-with-atomics rule
// (ADR 0011 §Phase 4 pack C, ADR 0013 sub-phase 4c). The rule fires on a
// hand-rolled prefix-sum (Hillis-Steele up-sweep with stride-doubling and
// barriers) or on per-lane atomic slot-claiming.
//
// Coverage:
//   * Hillis-Steele scan loop => fires.
//   * `InterlockedAdd(counter, 1, out)` slot claim => fires.
//   * Plain compute loop (no scan structure) => does not fire.
//   * Tree reduction with halving stride (manual-wave-reduction territory) =>
//     does not fire here.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_wave_prefix_sum_vs_scan_with_atomics();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_wave_prefix_sum_vs_scan_with_atomics());
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

TEST_CASE("wave-prefix-sum-vs-scan-with-atomics fires on Hillis-Steele scan loop",
          "[rules][wave-prefix-sum-vs-scan-with-atomics]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared uint g_Scan[64];

[numthreads(64, 1, 1)]
void cs_manual_scan(uint gi : SV_GroupIndex)
{
    g_Scan[gi] = gi;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 1; stride < 64; stride <<= 1) {
        uint v = g_Scan[gi - stride];
        GroupMemoryBarrierWithGroupSync();
        g_Scan[gi] += v;
        GroupMemoryBarrierWithGroupSync();
    }
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "wave-prefix-sum-vs-scan-with-atomics"));
}

TEST_CASE("wave-prefix-sum-vs-scan-with-atomics fires on InterlockedAdd slot-claim",
          "[rules][wave-prefix-sum-vs-scan-with-atomics]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWByteAddressBuffer g_Counter;
RWStructuredBuffer<uint> g_Out;

[numthreads(64, 1, 1)]
void cs_slot_claim(uint gi : SV_GroupIndex)
{
    uint slot;
    InterlockedAdd(g_Counter[0], 1, slot);
    g_Out[slot] = gi;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "wave-prefix-sum-vs-scan-with-atomics"));
}

TEST_CASE("wave-prefix-sum-vs-scan-with-atomics does not fire on a plain compute loop",
          "[rules][wave-prefix-sum-vs-scan-with-atomics]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[numthreads(64, 1, 1)]
void cs_plain(uint gi : SV_GroupIndex)
{
    uint x = 0;
    for (uint i = 0; i < 4; ++i) {
        x += i;
    }
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wave-prefix-sum-vs-scan-with-atomics");
    }
}

TEST_CASE("wave-prefix-sum-vs-scan-with-atomics does not fire on halving-stride reduction",
          "[rules][wave-prefix-sum-vs-scan-with-atomics]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
groupshared float g_Reduce[32];

[numthreads(32, 1, 1)]
void cs_reduce(uint gi : SV_GroupIndex)
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
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wave-prefix-sum-vs-scan-with-atomics");
    }
}

TEST_CASE("wave-prefix-sum-vs-scan-with-atomics does not fire on plain InterlockedAdd",
          "[rules][wave-prefix-sum-vs-scan-with-atomics]") {
    SourceManager sources;
    // Two-arg InterlockedAdd with no captured output and constant other than 1
    // should not fire (does not match the slot-claim shape).
    const std::string hlsl = R"hlsl(
RWByteAddressBuffer g_Counter;

[numthreads(64, 1, 1)]
void cs_simple(uint gi : SV_GroupIndex)
{
    InterlockedAdd(g_Counter[0], gi);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wave-prefix-sum-vs-scan-with-atomics");
    }
}
