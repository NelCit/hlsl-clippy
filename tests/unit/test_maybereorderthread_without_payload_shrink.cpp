// Tests for the maybereorderthread-without-payload-shrink rule (Phase 7 Pack
// DXR; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_maybereorderthread_without_payload_shrink();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("mrt.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_maybereorderthread_without_payload_shrink());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("maybereorderthread-without-payload-shrink smoke (does not crash)",
          "[rules][maybereorderthread-without-payload-shrink]") {
    const std::string hlsl = R"hlsl(
struct P { float3 c; };

float3 raygen_helper(float3 origin) {
    float a = origin.x + 1.0;
    float b = origin.y + 2.0;
    float c = origin.z + 3.0;
    float d = origin.x * 4.0;
    MaybeReorderThread(0, 0);
    return float3(a, b, c) + float3(d, d, d);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
    SUCCEED("maybereorderthread-without-payload-shrink ran without crashing");
}

TEST_CASE("maybereorderthread-without-payload-shrink silent without the call",
          "[rules][maybereorderthread-without-payload-shrink]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target {
    float a = 1.0;
    float b = 2.0;
    return float4(a, b, 0, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "maybereorderthread-without-payload-shrink");
}
