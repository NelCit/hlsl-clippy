// End-to-end tests for gather-channel-narrowing.

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
    const auto src = sources.add_buffer("gcn.hlsl", hlsl);
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

TEST_CASE("gather-channel-narrowing fires on .Gather().r", "[rules][gather-channel-narrowing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float r = t.Gather(s, uv).r;
    return float4(r, 0, 0, 1);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "gather-channel-narrowing"));
}

TEST_CASE("gather-channel-narrowing does not fire on full Gather usage",
          "[rules][gather-channel-narrowing]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float4 g = t.Gather(s, uv);
    return g;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "gather-channel-narrowing"));
}
