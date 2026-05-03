// Tests for sample-use-no-interleave (Phase 8 v0.9 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_sample_use_no_interleave();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("suni.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_sample_use_no_interleave());
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

TEST_CASE("sample-use-no-interleave fires when result is used in next statement",
          "[rules][sample-use-no-interleave]") {
    const std::string hlsl = R"hlsl(
Texture2D tex;
SamplerState ss;

float4 ps_main(float2 uv : TEXCOORD) : SV_Target {
    float4 c = tex.Sample(ss, uv);
    return c * 2.0;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "sample-use-no-interleave"));
}

TEST_CASE("sample-use-no-interleave silent with intervening compute (more than window)",
          "[rules][sample-use-no-interleave]") {
    const std::string hlsl = R"hlsl(
Texture2D tex;
SamplerState ss;

float4 ps_main(float2 uv : TEXCOORD, float a : TEXCOORD1, float b : TEXCOORD2) : SV_Target {
    float4 c = tex.Sample(ss, uv);
    float ax = a * 2.0f;
    float bx = b * 3.0f;
    float cx = ax + bx;
    float dx = cx * cx;
    return c + float4(ax, bx, cx, dx);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "sample-use-no-interleave"));
}
