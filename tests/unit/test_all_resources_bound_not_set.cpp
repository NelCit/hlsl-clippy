// End-to-end tests for all-resources-bound-not-set.

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
using hlsl_clippy::LintOptions;
using hlsl_clippy::make_default_rules;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("arb.hlsl", hlsl);
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

TEST_CASE("all-resources-bound-not-set is silent on a trivial shader",
          "[rules][all-resources-bound-not-set]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "all-resources-bound-not-set"));
}

TEST_CASE("all-resources-bound-not-set is silent when RootSignature attr is present",
          "[rules][all-resources-bound-not-set]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
#define MY_RS "RootSignature(\"DescriptorTable(SRV(t0, numDescriptors=4))\")"
Texture2D    a : register(t0);
Texture2D    b : register(t1);
Texture2D    c : register(t2);
Texture2D    d : register(t3);
SamplerState s : register(s0);

[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    return a.Sample(s, uv) + b.Sample(s, uv) + c.Sample(s, uv) + d.Sample(s, uv);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "all-resources-bound-not-set"));
}
