// End-to-end tests for the non-uniform-resource-index rule.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("nuri.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, opts);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("non-uniform-resource-index does not fire on a constant index",
          "[rules][non-uniform-resource-index]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D textures[8] : register(t0, space1);
SamplerState s        : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return textures[3].Sample(s, uv);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "non-uniform-resource-index"));
}

TEST_CASE("non-uniform-resource-index does not fire when marker is present",
          "[rules][non-uniform-resource-index]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D textures[8] : register(t0, space1);
SamplerState s        : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, uint matId : TEXCOORD1) : SV_Target {
    return textures[NonUniformResourceIndex(matId)].Sample(s, uv);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "non-uniform-resource-index"));
}

TEST_CASE("non-uniform-resource-index attaches a wrap Fix",
          "[rules][non-uniform-resource-index][fix]") {
    // The wrap is suggestion-grade: the rule cannot prove the index is
    // divergent at every call site. Wrapping a uniform index is harmless
    // but may impose a small waterfall overhead on some drivers.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D textures[8] : register(t0, space1);
SamplerState s        : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0, uint matId : TEXCOORD1) : SV_Target {
    return textures[matId].Sample(s, uv);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "non-uniform-resource-index") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK_FALSE(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement == "NonUniformResourceIndex(matId)");
    }
    CHECK(saw);
}
