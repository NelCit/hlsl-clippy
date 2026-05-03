// early-z-disabled-by-conditional-discard
//
// Detects `discard` / `clip` reachable from non-uniform CF in a pixel shader
// without the `[earlydepthstencil]` attribute on the entry point. Conditional
// discard makes the depth-test conservative; the `[earlydepthstencil]` hint
// tells the driver the discard cannot affect depth, allowing early-Z to fire.
//
// Stage: ControlFlow. Uses `cfg_query::inside_divergent_cf` to gate +
// `helper_lane_analyzer::in_pixel_stage_or_unknown` for the PS check, then
// scans the source text for `[earlydepthstencil]` to confirm absence.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/helper_lane_analyzer.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "early-z-disabled-by-conditional-discard";
constexpr std::string_view k_category = "control-flow";

void collect_discard_or_clip(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    const auto k = node_kind(node);
    if (k == "discard_statement") {
        out.push_back(node);
    } else if (k == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == "clip") {
            out.push_back(node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_discard_or_clip(::ts_node_child(node, i), bytes, out);
    }
}

class EarlyZDisabledByConditionalDiscard : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        if (!util::in_pixel_stage_or_unknown(cfg))
            return;
        const auto bytes = tree.source_bytes();
        // Skip when `[earlydepthstencil]` already appears anywhere in the
        // source (per-entry-point check would tighten this; ADR 0013
        // helper-lane stage metadata not yet wired).
        if (bytes.find("[earlydepthstencil]") != std::string_view::npos ||
            bytes.find("earlydepthstencil") != std::string_view::npos) {
            return;
        }
        std::vector<::TSNode> nodes;
        collect_discard_or_clip(::ts_tree_root_node(tree.raw_tree()), bytes, nodes);
        for (const auto n : nodes) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(n)};
            if (!util::inside_divergent_cf(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "`discard` / `clip` reachable from non-uniform CF without "
                "`[earlydepthstencil]` -- conditional discard makes the depth "
                "test conservative; early-Z is disabled and depth/stencil "
                "writes serialise the pipeline"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "annotate the pixel-shader entry point with "
                "`[earlydepthstencil]` if the discard cannot affect depth -- "
                "lets early-Z fire on RDNA / Ada / Xe-HPG"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_early_z_disabled_by_conditional_discard() {
    return std::make_unique<EarlyZDisabledByConditionalDiscard>();
}

}  // namespace shader_clippy::rules
