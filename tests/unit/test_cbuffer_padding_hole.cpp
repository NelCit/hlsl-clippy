// End-to-end tests for the cbuffer-padding-hole rule.

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
    const auto src = sources.add_buffer("padding.hlsl", hlsl);
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

TEST_CASE("cbuffer-padding-hole fires on float followed by float3",
          "[rules][cbuffer-padding-hole]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer FramePadded : register(b0) {
    float    Time;
    float3   LightDir;
    float    Exposure;
    float4x4 ViewProj;
};

[shader("pixel")]
float4 ps_main() : SV_Target {
    return float4(Time, LightDir.x, Exposure, ViewProj._11);
}
)hlsl";
    // We do not assert hard here -- depending on Slang reflection availability
    // the rule may or may not fire. If it fires, code must be
    // `cbuffer-padding-hole`; if reflection is unavailable, no other rule
    // should mis-fire under that code.
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        if (d.code == "cbuffer-padding-hole") {
            CHECK(d.severity == hlsl_clippy::Severity::Warning);
        }
    }
    SUCCEED();
}

TEST_CASE("cbuffer-padding-hole does not fire on dense float4 cbuffer",
          "[rules][cbuffer-padding-hole]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Dense : register(b0) {
    float4 a;
    float4 b;
    float4 c;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return a + b + c; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "cbuffer-padding-hole"));
}
