// End-to-end tests for the compute-dispatch-grid-shape-vs-quad rule
// (Pack C, Phase 3 reflection-aware).
//
// The rule fires on `ddx`/`ddy` calls inside a compute kernel declared
// `[numthreads(N, 1, 1)]` (1D dispatch shape). Compute-quad derivatives
// expect a 2x2 X/Y quad; a 1D shape produces nonsense neighbours.

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
[[nodiscard]] std::unique_ptr<Rule> make_compute_dispatch_grid_shape_vs_quad();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl,
                                                  SourceManager& sources,
                                                  const std::string& path = "synthetic.hlsl") {
    const auto src = sources.add_buffer(path, hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_compute_dispatch_grid_shape_vs_quad());
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

TEST_CASE("compute-dispatch-grid-shape-vs-quad fires on ddx in 1D numthreads",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float x = (float)tid.x * 0.1;
    output_buffer[tid.x] = ddx(x);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "compute-dispatch-grid-shape-vs-quad"));
}

TEST_CASE("compute-dispatch-grid-shape-vs-quad fires on ddy in 1D numthreads",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(32, 1, 1)]
void cs_grad(uint3 tid : SV_DispatchThreadID)
{
    float v = (float)tid.x * 0.5;
    output_buffer[tid.x] = ddy(v);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "compute-dispatch-grid-shape-vs-quad"));
}

TEST_CASE("compute-dispatch-grid-shape-vs-quad does not fire on 2D numthreads",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float x = (float)tid.x * 0.1;
    output_buffer[tid.x] = ddx(x);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compute-dispatch-grid-shape-vs-quad");
    }
}

TEST_CASE("compute-dispatch-grid-shape-vs-quad does not fire when no derivatives are used",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
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
        CHECK(d.code != "compute-dispatch-grid-shape-vs-quad");
    }
}

TEST_CASE("compute-dispatch-grid-shape-vs-quad does not fire on a pixel shader using ddx",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
    SourceManager sources;
    // Pixel shaders legitimately use ddx/ddy and have no [numthreads].
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target
{
    return float4(ddx(uv.x), ddy(uv.y), 0.0, 1.0);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        CHECK(d.code != "compute-dispatch-grid-shape-vs-quad");
    }
}

TEST_CASE("compute-dispatch-grid-shape-vs-quad fires on ddx_fine variant",
          "[rules][compute-dispatch-grid-shape-vs-quad]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
RWStructuredBuffer<float> output_buffer;

[shader("compute")]
[numthreads(64, 1, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    float x = (float)tid.x * 0.25;
    output_buffer[tid.x] = ddx_fine(x);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    CHECK(has_rule(diags, "compute-dispatch-grid-shape-vs-quad"));
}
