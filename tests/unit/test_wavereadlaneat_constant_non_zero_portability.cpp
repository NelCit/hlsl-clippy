// End-to-end tests for the wavereadlaneat-constant-non-zero-portability rule
// (Pack C, Phase 3 reflection-aware).
//
// The rule fires on `WaveReadLaneAt(x, K)` calls with constant non-zero `K`
// inside an entry point that does NOT pin the wave size via `[WaveSize]`.

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
[[nodiscard]] std::unique_ptr<Rule> make_wavereadlaneat_constant_non_zero_portability();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources,
                                                  const std::string& path = "synthetic.hlsl") {
    const auto src = sources.add_buffer(path, hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_wavereadlaneat_constant_non_zero_portability());
    LintOptions options;
    options.enable_reflection = true;
    options.target_profile = std::string{"sm_6_6"};
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

TEST_CASE(
    "wavereadlaneat-constant-non-zero-portability fires on WaveReadLaneAt(x, 31) without "
    "WaveSize",
    "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 31);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "wavereadlaneat-constant-non-zero-portability"));
}

TEST_CASE(
    "wavereadlaneat-constant-non-zero-portability does not fire when WaveSize attribute is present",
    "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(64)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 47);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavereadlaneat-constant-non-zero-portability");
    }
}

TEST_CASE("wavereadlaneat-constant-non-zero-portability does not fire on WaveReadLaneAt(x, 0)",
          "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavereadlaneat-constant-non-zero-portability");
    }
}

TEST_CASE("wavereadlaneat-constant-non-zero-portability does not fire on dynamic lane index",
          "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, tid.x & 31);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavereadlaneat-constant-non-zero-portability");
    }
}

TEST_CASE("wavereadlaneat-constant-non-zero-portability fires on small constant lane index",
          "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    // Even small non-zero indices fire because the rule's purpose is to
    // surface the unpinned-wave-size portability concern, not to bound-check
    // the index against any specific wave size.
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 7);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "wavereadlaneat-constant-non-zero-portability"));
}

TEST_CASE("wavereadlaneat-constant-non-zero-portability fires on hex constant lane index",
          "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 0x1f);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "wavereadlaneat-constant-non-zero-portability"));
}

TEST_CASE(
    "wavereadlaneat-constant-non-zero-portability emits a suggestion-grade diagnostic with no fix",
    "[rules][wavereadlaneat-constant-non-zero-portability]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveReadLaneAt(v, 16);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "wavereadlaneat-constant-non-zero-portability") {
            hit = &d;
            break;
        }
    }
    REQUIRE(hit != nullptr);
    CHECK(hit->fixes.empty());
}
