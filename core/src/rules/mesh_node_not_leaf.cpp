// mesh-node-not-leaf
//
// Detects a work-graph mesh node (a function with `[NodeLaunch("mesh")]`)
// that has any outgoing `NodeOutput<...>` parameter declarations. Mesh
// nodes must be leaf nodes per the preview Mesh Nodes spec.
//
// Stage: Ast.
//
// CONFIG-GATED: this rule is preview / experimental and per ADR 0010 must be
// gated behind `[experimental] work-graph-mesh-nodes = true` in
// `.shader-clippy.toml`. The current `Config` does not yet surface that key,
// so this rule's `on_tree` returns early unconditionally -- it never fires
// until the gate is wired through. The detection logic is in place; only
// the gate is missing.

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

constexpr std::string_view k_rule_id = "mesh-node-not-leaf";
constexpr std::string_view k_category = "work-graphs";

/// Until `Config` surfaces `[experimental] work-graph-mesh-nodes`, every mesh-
/// node rule reads through this constant. Flip to `true` when the gate is
/// wired -- the orchestrator can pass the config flag here.
constexpr bool k_experimental_mesh_nodes_enabled = false;

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const bool is_mesh_node = fn_text.find("NodeLaunch(\"mesh\")") != std::string_view::npos;
        if (is_mesh_node) {
            const auto body_pos = fn_text.find('{');
            const auto sig =
                (body_pos != std::string_view::npos) ? fn_text.substr(0, body_pos) : fn_text;
            if (sig.find("NodeOutput<") != std::string_view::npos) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "mesh nodes must be leaf nodes; this `[NodeLaunch(\"mesh\")]` "
                    "function declares a `NodeOutput<...>` (preview Mesh Nodes spec)"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class MeshNodeNotLeaf : public Rule {
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
            return;  // gated; awaits Config plumbing for the experimental key.
        }
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_mesh_node_not_leaf() {
    return std::make_unique<MeshNodeNotLeaf>();
}

}  // namespace shader_clippy::rules
