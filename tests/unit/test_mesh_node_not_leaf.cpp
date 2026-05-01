// Tests for the mesh-node-not-leaf rule (gated behind
// `[experimental] work-graph-mesh-nodes` -- the rule returns early until
// Config plumbs the flag through, so these tests assert the gate is closed).

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules {
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_not_leaf();
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
    rules.push_back(hlsl_clippy::rules::make_mesh_node_not_leaf());
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
