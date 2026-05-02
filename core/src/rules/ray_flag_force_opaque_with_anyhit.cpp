// ray-flag-force-opaque-with-anyhit
//
// Detects `TraceRay(...)` calls with `RAY_FLAG_FORCE_OPAQUE` set when the
// same source also defines a `[shader("anyhit")]` entry point. The
// flag skips AnyHit invocation, so binding an AnyHit and forcing opaque
// is dead code or a logic bug -- the AnyHit shader will never run.
//
// Stage: Reflection. We need to know whether an anyhit entry point
// exists in the same translation unit to gate the diagnostic.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "ray-flag-force-opaque-with-anyhit";
constexpr std::string_view k_category = "dxr";

void walk_traceray_calls(::TSNode node,
                         std::string_view bytes,
                         const AstTree& tree,
                         RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "TraceRay") {
            const auto call_text = node_text(node, bytes);
            if (call_text.find("RAY_FLAG_FORCE_OPAQUE") != std::string_view::npos) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message =
                    "`TraceRay(...)` with `RAY_FLAG_FORCE_OPAQUE` in a TU that also defines "
                    "an `[shader(\"anyhit\")]` entry point -- the AnyHit shader is bound "
                    "but the flag skips AnyHit invocation, leaving the shader as dead "
                    "code or revealing a logic bug";
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk_traceray_calls(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class RayFlagForceOpaqueWithAnyHit : public Rule {
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
        const auto bytes = tree.source_bytes();
        // Self-gate on the anyhit entry-point textual marker.
        const bool has_anyhit = bytes.find("\"anyhit\"") != std::string_view::npos;
        if (!has_anyhit) {
            return;
        }
        walk_traceray_calls(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_ray_flag_force_opaque_with_anyhit() {
    return std::make_unique<RayFlagForceOpaqueWithAnyHit>();
}

}  // namespace hlsl_clippy::rules
