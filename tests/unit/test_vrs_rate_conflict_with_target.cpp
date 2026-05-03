// Tests for vrs-rate-conflict-with-target (Phase 8 v0.9 pack; ADR 0018).

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
[[nodiscard]] std::unique_ptr<Rule> make_vrs_rate_conflict_with_target();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("vrsrc.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_vrs_rate_conflict_with_target());
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

TEST_CASE("vrs-rate-conflict-with-target fires when PS writes SV_ShadingRate AND a coarse-rate marker is present",
          "[rules][vrs-rate-conflict-with-target]") {
    const std::string hlsl = R"hlsl(
// Pretend: the project also tags this RT with PerPrimitive coarse rate.
// PerPrimitive
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
    CHECK(has_rule(lint_buffer(hlsl), "vrs-rate-conflict-with-target"));
}

TEST_CASE("vrs-rate-conflict-with-target silent without coarse-rate marker",
          "[rules][vrs-rate-conflict-with-target]") {
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
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "vrs-rate-conflict-with-target"));
}
