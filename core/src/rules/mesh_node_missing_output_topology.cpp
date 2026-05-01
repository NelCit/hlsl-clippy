// mesh-node-missing-output-topology
//
// Detects a mesh node (function annotated `[NodeLaunch("mesh")]`) that
// lacks the `[outputtopology(...)]` attribute, or whose value is not one of
// `"triangle"` / `"line"`.
//
// Stage: Ast.
//
// CONFIG-GATED: preview API, per ADR 0010 gated behind
// `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`.
// `Config` does not yet surface the key, so this rule returns early
// unconditionally until the gate is wired through.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "mesh-node-missing-output-topology";
constexpr std::string_view k_category = "work-graphs";

constexpr bool k_experimental_mesh_nodes_enabled = false;

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view extract_topology(std::string_view fn_text) noexcept {
    const auto attr = fn_text.find("outputtopology(");
    if (attr == std::string_view::npos) {
        return {};
    }
    const auto open = fn_text.find('"', attr);
    if (open == std::string_view::npos) {
        return {};
    }
    const auto close = fn_text.find('"', open + 1);
    if (close == std::string_view::npos) {
        return {};
    }
    return fn_text.substr(open + 1, close - open - 1);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const bool is_mesh_node = fn_text.find("NodeLaunch(\"mesh\")") != std::string_view::npos;
        if (is_mesh_node) {
            const auto topo = extract_topology(fn_text);
            const bool ok_topo = topo == "triangle" || topo == "line";
            if (!ok_topo) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "mesh nodes must declare `[outputtopology(\"triangle\")]` or "
                    "`[outputtopology(\"line\")]`; the rasterizer cannot wire the "
                    "node without it (preview Mesh Nodes spec)"};
                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class MeshNodeMissingOutputTopology : public Rule {
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

std::unique_ptr<Rule> make_mesh_node_missing_output_topology() {
    return std::make_unique<MeshNodeMissingOutputTopology>();
}

}  // namespace hlsl_clippy::rules
