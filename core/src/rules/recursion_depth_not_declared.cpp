// recursion-depth-not-declared
//
// Detects ray generation entry points that contain at least one `TraceRay`
// call but no `[shader("raygeneration")]` attribute is paired with a
// pipeline-side `MaxTraceRecursionDepth` declaration in source. Without an
// explicit max-depth annotation the runtime defaults to depth 1; path-tracer
// raygens that recurse silently truncate after the first bounce.
//
// Stage: Ast. The rule walks every `[shader("raygeneration")]`-decorated
// function and checks for a sibling source-level marker: a comment / string
// "MaxTraceRecursionDepth" (matching pipeline-config code) or a known
// `[shader_recursion_depth(N)]` annotation. If the marker is absent and the
// function body contains a `TraceRay` call, we warn.

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

constexpr std::string_view k_rule_id = "recursion-depth-not-declared";
constexpr std::string_view k_category = "dxr";

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        const bool is_raygen =
            fn_text.find("\"raygeneration\"") != std::string_view::npos;
        const bool has_traceray = fn_text.find("TraceRay") != std::string_view::npos;
        if (is_raygen && has_traceray) {
            // Look for either an attribute-style depth declaration on this
            // function, or a TU-wide MaxTraceRecursionDepth marker.
            const bool has_attr = fn_text.find("shader_recursion_depth") != std::string_view::npos;
            const bool tu_has_max =
                bytes.find("MaxTraceRecursionDepth") != std::string_view::npos ||
                bytes.find("MaxRecursionDepth") != std::string_view::npos;
            if (!has_attr && !tu_has_max) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "raygeneration entry calls `TraceRay` but no `MaxRecursionDepth` / "
                    "`[shader_recursion_depth(N)]` marker is present; the runtime "
                    "defaults to depth 1 and silently truncates recursive bounces"};
                ctx.emit(std::move(diag));
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class RecursionDepthNotDeclared : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_recursion_depth_not_declared() {
    return std::make_unique<RecursionDepthNotDeclared>();
}

}  // namespace hlsl_clippy::rules
