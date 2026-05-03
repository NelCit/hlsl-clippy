// End-to-end tests for the bool-straddles-16b rule.

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
    const auto src = sources.add_buffer("bool.hlsl", hlsl);
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

TEST_CASE("bool-straddles-16b is reported when present", "[rules][bool-straddles-16b]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer StraddleCB : register(b0) {
    float3 Tint;
    bool   UseTint;
    float4 More;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return UseTint ? float4(Tint, 1) : More; }
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    for (const auto& d : diags) {
        if (d.code == "bool-straddles-16b")
            CHECK(d.severity == shader_clippy::Severity::Error);
    }
    SUCCEED();
}

TEST_CASE("bool-straddles-16b does not fire on bool at offset 0", "[rules][bool-straddles-16b]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer OkCB : register(b0) {
    bool   Flag;
    float3 _pad;
    float4 Other;
};

[shader("pixel")]
float4 ps_main() : SV_Target { return Flag ? Other : float4(0,0,0,1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "bool-straddles-16b"));
}
