// mesh-node-uses-vertex-shader-pipeline
//
// Detects a `[NodeLaunch("mesh")]` entry whose pipeline subobject (named in
// the work-graph state-object configuration on the application side) is not
// a mesh-shader pipeline state. The PSO link fails on every preview driver.
//
// Stage: Ast (forward-compatible-stub).
//
// CONFIG-GATED: preview API, per ADR 0010 gated behind
// `[experimental] work-graph-mesh-nodes = true` in `.shader-clippy.toml`.
// `Config` does not yet surface the key, so this rule returns early
// unconditionally.
//
// Scope caveat: the actual mismatch can only be diagnosed against the
// application-side state-object configuration, which is not in shader
// reflection. The Phase 3 stub is a documentation anchor: it never fires
// today (the experimental gate is closed), and once the gate opens it will
// surface as a "verify" suggestion. Future Phase 4 work may surface the
// mismatch via a project-level state-object descriptor.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "mesh-node-uses-vertex-shader-pipeline";
constexpr std::string_view k_category = "work-graphs";

constexpr bool k_experimental_mesh_nodes_enabled = false;

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        if (fn_text.find("NodeLaunch(\"mesh\")") != std::string_view::npos) {
            // Surface a verify-grade reminder; the actual mismatch is on the
            // application side and not visible from the AST.
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "mesh-node entry: verify the application-side pipeline "
                "subobject is `D3D12_MESH_SHADER_PIPELINE_STATE_DESC` (not a "
                "VS+PS graphics PSO); preview Mesh Nodes spec rejects the "
                "legacy graphics path"};
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class MeshNodeUsesVertexShaderPipeline : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        if (!k_experimental_mesh_nodes_enabled) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_mesh_node_uses_vertex_shader_pipeline() {
    return std::make_unique<MeshNodeUsesVertexShaderPipeline>();
}

}  // namespace shader_clippy::rules
