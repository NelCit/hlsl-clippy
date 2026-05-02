// Tests for the output-count-overrun rule (Phase 7 Pack Mesh; ADR 0017).

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
[[nodiscard]] std::unique_ptr<Rule> make_output_count_overrun();
}

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::LintOptions;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("oco.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_output_count_overrun());
    LintOptions opts;
    opts.enable_control_flow = true;
    opts.enable_reflection = false;
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

TEST_CASE("output-count-overrun fires when literal count exceeds vertex ceiling",
          "[rules][output-count-overrun]") {
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(uint gi : SV_GroupIndex,
             out vertices V verts[64],
             out indices uint3 prims[64]) {
    SetMeshOutputCounts(96, 32);
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl), "output-count-overrun"));
}

TEST_CASE("output-count-overrun silent when literal counts respect the ceiling",
          "[rules][output-count-overrun]") {
    const std::string hlsl = R"hlsl(
struct V { float4 p : SV_Position; };
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(uint gi : SV_GroupIndex,
             out vertices V verts[64],
             out indices uint3 prims[64]) {
    SetMeshOutputCounts(64, 64);
}
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "output-count-overrun"));
}

TEST_CASE("output-count-overrun silent when SetMeshOutputCounts is absent",
          "[rules][output-count-overrun]") {
    const std::string hlsl = R"hlsl(
float4 ps_main() : SV_Target { return 0; }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl), "output-count-overrun"));
}
