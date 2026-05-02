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

TEST_CASE("gather-channel-narrowing attaches a machine-applicable Fix on `.r`",
          "[rules][gather-channel-narrowing][fix]") {
    // Regression for v0.6.8: rule was tagged `applicability: machine-applicable`
    // in docs but emitted a Fix-less diagnostic. `.r/.x` rewrites are
    // bit-identical (Gather returns red[LL..UL]; GatherRed is the same
    // intrinsic semantically) so we mark the Fix machine-applicable.
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
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "gather-channel-narrowing") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement == "GatherRed(s, uv).r");
    }
    CHECK(saw);
}

TEST_CASE("gather-channel-narrowing downgrades Fix to suggestion-grade on `.g`",
          "[rules][gather-channel-narrowing][fix]") {
    // `.g` on a `Gather` result is the second-texel red value, NOT a
    // green-channel sample; the rule heuristically rewrites to
    // `GatherGreen(...).r` (first-texel green) because that is what the
    // developer most likely meant. This is a semantic shift, so the Fix is
    // suggestion-grade.
    SourceManager sources;
    const std::string hlsl = R"hlsl(
Texture2D    t : register(t0);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float g = t.Gather(s, uv).g;
    return float4(g, 0, 0, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    bool saw = false;
    for (const auto& d : diags) {
        if (d.code != "gather-channel-narrowing") {
            continue;
        }
        saw = true;
        REQUIRE(d.fixes.size() == 1U);
        const auto& fix = d.fixes.front();
        CHECK_FALSE(fix.machine_applicable);
        REQUIRE(fix.edits.size() == 1U);
        CHECK(fix.edits.front().replacement == "GatherGreen(s, uv).r");
    }
    CHECK(saw);
}
