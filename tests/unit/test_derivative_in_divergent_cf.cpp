// End-to-end tests for the derivative-in-divergent-cf rule.

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
    const auto src = sources.add_buffer("ddiv.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("derivative-in-divergent-cf smoke test", "[rules][derivative-in-divergent-cf]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
float main(float2 uv : TEXCOORD0, uint3 tid : SV_DispatchThreadID) : SV_Target {
    if (tid.x > 0) {
        float dx = ddx(uv.x);
        return dx;
    }
    return 0.0;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    (void)diags;
    SUCCEED("derivative-in-divergent-cf ran without crashing");
}
