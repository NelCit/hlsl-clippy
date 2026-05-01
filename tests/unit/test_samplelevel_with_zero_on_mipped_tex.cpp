// End-to-end tests for samplelevel-with-zero-on-mipped-tex.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("slz.hlsl", hlsl);
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

TEST_CASE("samplelevel-with-zero-on-mipped-tex fires on literal zero LOD",
          "[rules][samplelevel-with-zero-on-mipped-tex]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.SampleLevel(s, uv, 0);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "samplelevel-with-zero-on-mipped-tex"));
}

TEST_CASE("samplelevel-with-zero-on-mipped-tex does not fire on non-zero LOD",
          "[rules][samplelevel-with-zero-on-mipped-tex]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return t.SampleLevel(s, uv, 2);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "samplelevel-with-zero-on-mipped-tex"));
}
