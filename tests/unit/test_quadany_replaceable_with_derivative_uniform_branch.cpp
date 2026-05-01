// Tests for the quadany-replaceable-with-derivative-uniform-branch rule
// (Phase 4 Pack E). Forward-compatible-stub: fires only on `if (QuadAny|
// QuadAll(...))` whose body has no derivative-bearing operation at all,
// where the unwrap is unambiguously safe. Bodies that contain derivatives
// stay silent until the Phase 4 quad-uniformity oracle lands.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_quadany_replaceable_with_derivative_uniform_branch();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_quadany_replaceable_with_derivative_uniform_branch());
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

TEST_CASE("quadany-replaceable-with-derivative-uniform-branch fires when body has no derivatives",
          "[rules][quadany-replaceable-with-derivative-uniform-branch]") {
    const std::string hlsl = R"hlsl(
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0, 0, 0, 0);
    if (QuadAny(uv.y > 0.5)) {
        c = float4(1, 1, 1, 1);
    }
    return c;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "quadany-replaceable-with-derivative-uniform-branch"));
}

TEST_CASE("quadany-replaceable-with-derivative-uniform-branch fires for QuadAll wrapper",
          "[rules][quadany-replaceable-with-derivative-uniform-branch]") {
    const std::string hlsl = R"hlsl(
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0, 0, 0, 0);
    if (QuadAll(uv.y > 0.5)) {
        c = float4(1, 0, 0, 1);
    }
    return c;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "quadany-replaceable-with-derivative-uniform-branch"));
}

TEST_CASE("quadany-replaceable-with-derivative-uniform-branch is silent when body samples",
          "[rules][quadany-replaceable-with-derivative-uniform-branch]") {
    const std::string hlsl = R"hlsl(
Texture2D<float4> g_Atlas : register(t0);
SamplerState g_Sampler : register(s0);
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0, 0, 0, 0);
    if (QuadAny(uv.y > 0.5)) {
        c = g_Atlas.Sample(g_Sampler, uv);
    }
    return c;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "quadany-replaceable-with-derivative-uniform-branch");
    }
}

TEST_CASE("quadany-replaceable-with-derivative-uniform-branch is silent on bare `if`",
          "[rules][quadany-replaceable-with-derivative-uniform-branch]") {
    const std::string hlsl = R"hlsl(
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0, 0, 0, 0);
    if (uv.y > 0.5) {
        c = float4(1, 1, 1, 1);
    }
    return c;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "quadany-replaceable-with-derivative-uniform-branch");
    }
}
