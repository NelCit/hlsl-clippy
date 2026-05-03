// End-to-end tests for excess-interpolators.

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
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("ei.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("excess-interpolators silent on a small struct", "[rules][excess-interpolators]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct VsOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "excess-interpolators"));
}

TEST_CASE("excess-interpolators fires on > 16 float4 interpolators",
          "[rules][excess-interpolators]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct VsOut {
    float4 t0  : TEXCOORD0;
    float4 t1  : TEXCOORD1;
    float4 t2  : TEXCOORD2;
    float4 t3  : TEXCOORD3;
    float4 t4  : TEXCOORD4;
    float4 t5  : TEXCOORD5;
    float4 t6  : TEXCOORD6;
    float4 t7  : TEXCOORD7;
    float4 t8  : TEXCOORD8;
    float4 t9  : TEXCOORD9;
    float4 t10 : TEXCOORD10;
    float4 t11 : TEXCOORD11;
    float4 t12 : TEXCOORD12;
    float4 t13 : TEXCOORD13;
    float4 t14 : TEXCOORD14;
    float4 t15 : TEXCOORD15;
    float4 t16 : TEXCOORD16;
    float4 t17 : TEXCOORD17;
};
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "excess-interpolators"));
}
