// Tests for the mesh-node-not-leaf rule (gated behind
// `[experimental] work-graph-mesh-nodes` -- the rule returns early until
// Config plumbs the flag through, so these tests assert the gate is closed).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/lint.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_not_leaf();
}  // namespace shader_clippy::rules

namespace {

using shader_clippy::Diagnostic;
using shader_clippy::lint;
using shader_clippy::Rule;
using shader_clippy::SourceManager;

[[nodiscard]] std::vector<Diagnostic> lint_buffer(const std::string& hlsl) {
    SourceManager sources;
    const auto src = sources.add_buffer("synthetic.hlsl", hlsl);
    REQUIRE(src.valid());
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(shader_clippy::rules::make_mesh_node_not_leaf());
    return lint(sources, src, rules);
}

}  // namespace

TEST_CASE("mesh-node-not-leaf is silent under the closed experimental gate",
          "[rules][mesh-node-not-leaf]") {
    const std::string hlsl = R"hlsl(
[Shader("node")]
[NodeLaunch("mesh")]
void MeshNode() {
    NodeOutput<int> downstream;
}
)hlsl";
    const auto diags = lint_buffer(hlsl);
    for (const auto& d : diags) {
        CHECK(d.code != "mesh-node-not-leaf");
    }
}
