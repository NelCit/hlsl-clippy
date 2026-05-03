// Tests for the vgpr-pressure-warning rule (Phase 7 Pack Pressure; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_vgpr_pressure_warning();
}

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::LintOptions;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("vpw.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_vgpr_pressure_warning());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("vgpr-pressure-warning smoke (does not crash)", "[rules][vgpr-pressure-warning]") {
    const std::string hlsl = R"hlsl(
float many_floats(float input) {
    float a0 = input + 0.0;
    float a1 = input + 1.0;
    float a2 = input + 2.0;
    float a3 = input + 3.0;
    float a4 = input + 4.0;
    return a0 + a1 + a2 + a3 + a4;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    (void)diags;
    SUCCEED("vgpr-pressure-warning ran without crashing");
}

TEST_CASE("vgpr-pressure-warning silent on a tiny shader", "[rules][vgpr-pressure-warning]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags)
        CHECK(d.code != "vgpr-pressure-warning");
}
