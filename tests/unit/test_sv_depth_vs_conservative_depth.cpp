// End-to-end tests for sv-depth-vs-conservative-depth.

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
using shader_clippy::make_default_rules;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl, SourceManager& sources) {
    const auto src = sources.add_buffer("svdepth.hlsl", hlsl);
    REQUIRE(src.valid());
    auto rules = make_default_rules();
    return lint(sources, src, rules);
}

[[nodiscard]] bool has_rule(const std::vector<Diagnostic>& diags, std::string_view code) {
    for (const auto& d : diags) {
        if (d.code == code)
            return true;
    }
    return false;
}

}  // namespace

TEST_CASE("sv-depth-vs-conservative-depth fires on SV_Depth output",
          "[rules][sv-depth-vs-conservative-depth]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
struct PsOut {
    float4 color : SV_Target0;
    float  depth : SV_Depth;
};

[shader("pixel")]
PsOut ps_main(float4 pos : SV_Position) {
    PsOut o;
    o.color = float4(0, 0, 0, 1);
    o.depth = pos.z + 0.001;
    return o;
}
)hlsl";
    CHECK(has_rule(lint_buffer(hlsl, sources), "sv-depth-vs-conservative-depth"));
}

TEST_CASE("sv-depth-vs-conservative-depth silent without SV_Depth",
          "[rules][sv-depth-vs-conservative-depth]") {
    SourceManager sources;
    const std::string hlsl = R"hlsl(
[shader("pixel")]
float4 ps_main() : SV_Target { return float4(0, 0, 0, 1); }
)hlsl";
    CHECK_FALSE(has_rule(lint_buffer(hlsl, sources), "sv-depth-vs-conservative-depth"));
}
