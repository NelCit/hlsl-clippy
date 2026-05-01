// Tests for the mesh-node-uses-vertex-shader-pipeline rule (gated; rule
// returns early until config plumbs the experimental flag).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_uses_vertex_shader_pipeline();
}  // namespace hlsl_clippy::rules

namespace {

using hlsl_clippy::Diagnostic;
using hlsl_clippy::lint;
using hlsl_clippy::Rule;
using hlsl_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(hlsl_clippy::rules::make_mesh_node_uses_vertex_shader_pipeline());
    return lint(sources, src, rules);
}

}  // namespace

TEST_CASE("mesh-node-uses-vertex-shader-pipeline is silent under the closed experimental gate",
          "[rules][mesh-node-uses-vertex-shader-pipeline]") {
    const std::string hlsl = R"hlsl(
[Shader("node")]
[NodeLaunch("mesh")]
void MeshNode() {
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "mesh-node-uses-vertex-shader-pipeline");
    }
}
