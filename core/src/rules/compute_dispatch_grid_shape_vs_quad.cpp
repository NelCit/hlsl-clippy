// compute-dispatch-grid-shape-vs-quad
//
// Detects a compute kernel declared with `[numthreads(N, 1, 1)]` that reads
// `ddx` / `ddy` (compute-quad derivatives, available from SM 6.6 onward).
// The compute-quad model expects a 2x2 quad in the X/Y plane: a 1D dispatch
// shape produces nonsense derivatives because the quad neighbours fall on
// adjacent lanes that don't share a row/column relationship in source space.
//
// Detection (Reflection-stage):
//   For each compute entry point in reflection,
//     - confirm `numthreads.y == 1 && numthreads.z == 1` (1D dispatch);
//     - walk the AST under the function body looking for a `call_expression`
//       whose function identifier is `ddx`, `ddy`, `ddx_fine`, `ddy_fine`,
//       `ddx_coarse`, or `ddy_coarse`.
//   On a match, emit a suggestion-grade diagnostic anchored at the
//   derivative call site. No fix -- changing the dispatch shape requires a
//   coordinated host-side change to the `Dispatch(x, y, z)` arguments.
//
// Stage: Reflection (needs entry-point info to confirm compute stage +
// numthreads). Walks the AST to anchor the diagnostic at the derivative call.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_stage.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "compute-dispatch-grid-shape-vs-quad";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_derivative_intrinsic(std::string_view name) noexcept {
    return name == "ddx" || name == "ddy" || name == "ddx_fine" || name == "ddy_fine" ||
           name == "ddx_coarse" || name == "ddy_coarse";
}

void scan_for_derivatives(::TSNode node,
                          std::string_view bytes,
                          const AstTree& tree,
                          std::string_view entry_name,
                          const std::array<std::uint32_t, 3>& numthreads,
                          RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_name = node_text(fn, bytes);
        if (is_derivative_intrinsic(fn_name)) {
            const auto call_range = tree.byte_range(node);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
            diag.message = std::string{"`"} + std::string{fn_name} +
                           std::string{"` requires a 2x2 thread quad but compute entry point `"} +
                           std::string{entry_name} + std::string{"` is declared `[numthreads("} +
                           std::to_string(numthreads[0]) + std::string{", "} +
                           std::to_string(numthreads[1]) + std::string{", "} +
                           std::to_string(numthreads[2]) +
                           std::string{
                               ")]` -- a 1D dispatch shape produces nonsense "
                               "derivatives because adjacent lanes do not share an X/Y "
                               "quad neighbourhood"};
            // Suggestion-grade: no fix. Changing the dispatch shape coordinates
            // with the host-side `Dispatch` call.
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_for_derivatives(::ts_node_child(node, i), bytes, tree, entry_name, numthreads, ctx);
    }
}

/// Recursively check whether `node` contains an identifier descendant whose
/// text equals `target_name`. Used to associate a function_definition node
/// with an entry-point name.
[[nodiscard]] bool contains_identifier(::TSNode node,
                                       std::string_view bytes,
                                       std::string_view target_name) noexcept {
    if (::ts_node_is_null(node)) {
        return false;
    }
    if (node_kind(node) == "identifier" && node_text(node, bytes) == target_name) {
        return true;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (contains_identifier(::ts_node_child(node, i), bytes, target_name)) {
            return true;
        }
    }
    return false;
}

/// Locate the function_definition for `entry_name` by walking the tree and
/// returning the first definition whose subtree contains an identifier
/// matching the entry name. This is a deliberately defensive walk that
/// works around tree-sitter-hlsl grammar gaps around `[attr]` prefixes.
[[nodiscard]] ::TSNode find_entry_function(::TSNode root,
                                           std::string_view bytes,
                                           std::string_view entry_name) noexcept {
    if (::ts_node_is_null(root)) {
        return root;
    }
    if (node_kind(root) == "function_definition" && contains_identifier(root, bytes, entry_name)) {
        return root;
    }
    const std::uint32_t count = ::ts_node_child_count(root);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ::TSNode hit = find_entry_function(::ts_node_child(root, i), bytes, entry_name);
        if (!::ts_node_is_null(hit)) {
            return hit;
        }
    }
    return ::TSNode{};
}

class ComputeDispatchGridShapeVsQuad : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        // ADR 0020 sub-phase A v1.3.1 — needs the AST to find SV_DispatchThreadID
        // / SV_GroupThreadID parameter use sites. Bail silently when no tree
        // is available (`.slang` until sub-phase B).
        if (tree.raw_tree() == nullptr) {
            return;
        }
        const std::string_view bytes = tree.source_bytes();
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        if (::ts_node_is_null(root)) {
            return;
        }

        for (const auto& ep : reflection.entry_points) {
            if (!util::is_compute_shader(ep)) {
                continue;
            }
            if (!ep.numthreads.has_value()) {
                continue;
            }
            const auto& nt = *ep.numthreads;
            // 1D shape: y == 1 AND z == 1. The X dimension can be anything.
            if (nt[1] != 1U || nt[2] != 1U) {
                continue;
            }
            const ::TSNode fn = find_entry_function(root, bytes, ep.name);
            if (::ts_node_is_null(fn)) {
                continue;
            }
            scan_for_derivatives(fn, bytes, tree, ep.name, nt, ctx);
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_compute_dispatch_grid_shape_vs_quad() {
    return std::make_unique<ComputeDispatchGridShapeVsQuad>();
}

}  // namespace shader_clippy::rules
