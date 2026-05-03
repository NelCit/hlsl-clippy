// Tests for vrs-without-perprimitive-or-screenspace-source (Phase 8 v0.9 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_vrs_without_perprimitive_or_screenspace_source();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("vrsps.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_vrs_without_perprimitive_or_screenspace_source());
    LintOptions opts;
    opts.enable_reflection = true;
    opts.target_profile = std::string{"ps_6_6"};
    return lint(sources, src, rules, opts);
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

TEST_CASE("vrs-without-perprimitive-or-screenspace-source fires on a PS that emits SV_ShadingRate without upstream",
          "[rules][vrs-without-perprimitive-or-screenspace-source]") {
    const std::string hlsl = R"hlsl(
struct PSOut {
    float4 color : SV_Target;
    uint rate : SV_ShadingRate;
};
PSOut ps_main() {
    PSOut o;
    o.color = float4(1, 0, 0, 1);
    o.rate = 0;
    return o;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "vrs-without-perprimitive-or-screenspace-source"));
}

TEST_CASE("vrs-without-perprimitive-or-screenspace-source silent with [earlydepthstencil]",
          "[rules][vrs-without-perprimitive-or-screenspace-source]") {
    const std::string hlsl = R"hlsl(
[earlydepthstencil]
float4 ps_main() : SV_Target {
    return float4(0, 0, 0, 1);
}
struct PSOut2 {
    float4 color : SV_Target;
    uint rate : SV_ShadingRate;
};
PSOut2 ps_main2() {
    PSOut2 o;
    o.color = float4(1, 0, 0, 1);
    o.rate = 0;
    return o;
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl),
                         "vrs-without-perprimitive-or-screenspace-source"));
}
