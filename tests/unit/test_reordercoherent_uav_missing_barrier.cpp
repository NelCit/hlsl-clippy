// Tests for the reordercoherent-uav-missing-barrier rule (Phase 4 Pack E).
// Forward-compatible-stub: detects the textual write -> reorder -> read
// pattern across `[reordercoherent]` UAVs without an intervening barrier.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_reordercoherent_uav_missing_barrier();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_reordercoherent_uav_missing_barrier());
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

TEST_CASE("reordercoherent-uav-missing-barrier fires on write/reorder/read without barrier",
          "[rules][reordercoherent-uav-missing-barrier]") {
    const std::string hlsl = R"hlsl(
[reordercoherent] RWStructuredBuffer<float> g_Cache : register(u0);
void RayGen(dx::HitObject hit) {
    uint laneId = WaveGetLaneIndex();
    g_Cache[laneId] = 1.0;
    dx::MaybeReorderThread(hit);
    float v = g_Cache[laneId];
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "reordercoherent-uav-missing-barrier"));
}

TEST_CASE("reordercoherent-uav-missing-barrier is silent when DeviceMemoryBarrier is present",
          "[rules][reordercoherent-uav-missing-barrier]") {
    const std::string hlsl = R"hlsl(
[reordercoherent] RWStructuredBuffer<float> g_Cache : register(u0);
void RayGen(dx::HitObject hit) {
    uint laneId = WaveGetLaneIndex();
    g_Cache[laneId] = 1.0;
    DeviceMemoryBarrier();
    dx::MaybeReorderThread(hit);
    float v = g_Cache[laneId];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "reordercoherent-uav-missing-barrier");
    }
}

TEST_CASE("reordercoherent-uav-missing-barrier is silent on non-reordercoherent UAVs",
          "[rules][reordercoherent-uav-missing-barrier]") {
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> g_Cache : register(u0);
void RayGen(dx::HitObject hit) {
    uint laneId = WaveGetLaneIndex();
    g_Cache[laneId] = 1.0;
    dx::MaybeReorderThread(hit);
    float v = g_Cache[laneId];
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "reordercoherent-uav-missing-barrier");
    }
}
