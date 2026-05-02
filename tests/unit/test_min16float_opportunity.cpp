// Tests for the min16float-opportunity rule (Phase 7 Pack Precision; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_min16float_opportunity();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("m16o.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_min16float_opportunity());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"sm_6_6"};
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("min16float-opportunity smoke (does not crash)",
          "[rules][min16float-opportunity]") {
    const std::string hlsl = R"hlsl(
float4 ps_main(float2 uv : TEXCOORD) : SV_Target {
    half h_input = (half)uv.x;
    float w = (float)h_input * 0.5;
    return float4(w, w, w, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
    SUCCEED("min16float-opportunity ran without crashing");
}

TEST_CASE("min16float-opportunity silent on plain-float pipelines",
          "[rules][min16float-opportunity]") {
    const std::string hlsl = R"hlsl(
float4 ps_main(float2 uv : TEXCOORD) : SV_Target {
    return float4(uv, 0, 1);
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "min16float-opportunity");
}
