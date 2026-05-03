// End-to-end tests for the wavesize-attribute-missing rule
// (Pack C, Phase 3 reflection-aware).
//
// The rule fires on entry points that call `Wave*` intrinsics but do NOT
// pin the wave size via `[WaveSize(N)]` or `[WaveSize(min, max)]`.

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
[[nodiscard]] std::unique_ptr<Rule> make_wavesize_attribute_missing();
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
    rules.push_back(shader_clippy::rules::make_wavesize_attribute_missing());
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

TEST_CASE("wavesize-attribute-missing fires on compute kernel using WaveActiveSum without WaveSize",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveActiveSum(v);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "wavesize-attribute-missing"));
}

TEST_CASE("wavesize-attribute-missing does not fire when WaveSize attribute is present",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveActiveSum(v);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavesize-attribute-missing");
    }
}

TEST_CASE("wavesize-attribute-missing does not fire when WaveSize range is present",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
[WaveSize(32, 64)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    output_buffer[tid.x] = WaveGetLaneCount();
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavesize-attribute-missing");
    }
}

TEST_CASE("wavesize-attribute-missing does not fire on kernels not using wave intrinsics",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    output_buffer[tid.x] = (float)tid.x;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "wavesize-attribute-missing");
    }
}

TEST_CASE("wavesize-attribute-missing fires on WaveActiveBallot",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<uint4> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    output_buffer[tid.x] = WaveActiveBallot(tid.x > 8);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "wavesize-attribute-missing"));
}

TEST_CASE("wavesize-attribute-missing emits a suggestion-grade diagnostic with no fix",
          "[rules][wavesize-attribute-missing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x;
    output_buffer[tid.x] = WaveActiveSum(v);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    const Diagnostic* hit = nullptr;
    for (const auto& d : diags) {
        if (d.code == "wavesize-attribute-missing") {
            hit = &d;
            break;
        }
    }
    REQUIRE(hit != nullptr);
    CHECK(hit->fixes.empty());
}
