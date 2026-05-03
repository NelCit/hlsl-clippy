// End-to-end tests for the cbuffer-load-in-loop rule.

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
    const auto src = sources.add_buffer("cbil.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
    return lint(sources, src, rules, opts);
}

}  // namespace

TEST_CASE("cbuffer-load-in-loop smoke test", "[rules][cbuffer-load-in-loop]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
cbuffer Cb {
    float4 settings;
};

float main() : SV_Target {
    float acc = 0.0;
    for (int i = 0; i < 4; ++i) {
        acc = acc + settings.x;
    }
    return acc;
}
)hlsl";
    const auto diags = lint_buffer(hlsl, sources);
    (void)diags;
    SUCCEED("cbuffer-load-in-loop ran without crashing");
}
