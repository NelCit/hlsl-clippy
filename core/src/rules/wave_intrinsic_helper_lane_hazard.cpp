// wave-intrinsic-helper-lane-hazard
//
// Detects wave intrinsics dispatched from a program point that may be a
// helper lane (post-`discard` reachable on at least one CFG path). Helper
// lanes participate in derivative + quad-uniform ops but their contribution
// to wave reductions is undefined per the SM 6.x spec.
//
// Stage: ControlFlow. Uses `helper_lane_analyzer::possibly_helper_lane_at`
// + `in_pixel_stage_or_unknown`.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/helper_lane_analyzer.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "wave-intrinsic-helper-lane-hazard";
constexpr std::string_view k_category = "wave-helper-lane";

constexpr std::array<std::string_view, 14> k_wave_names{
    "WaveActiveSum",
    "WaveActiveProduct",
    "WaveActiveMin",
    "WaveActiveMax",
    "WaveActiveBitAnd",
    "WaveActiveBitOr",
    "WaveActiveBitXor",
    "WaveActiveAllTrue",
    "WaveActiveAnyTrue",
    "WaveActiveBallot",
    "WaveActiveAllEqual",
    "WaveActiveCountBits",
    "WavePrefixSum",
    "WavePrefixProduct",
};

void collect_wave_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        for (const auto name : k_wave_names) {
            if (fn_text == name) {
                out.push_back(node);
                break;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_wave_calls(::ts_node_child(node, i), bytes, out);
    }
}

class WaveIntrinsicHelperLaneHazard : public Rule {
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
        std::vector<::TSNode> calls;
        collect_wave_calls(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::possibly_helper_lane_at(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "wave intrinsic in PS reachable on a path that may have already "
                "discarded -- helper-lane participation in wave reductions is "
                "undefined per the SM 6.x spec"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "hoist the wave intrinsic above any reachable `discard` / "
                "`clip`, or guard with `WaveIsHelperLane()` to skip on helper "
                "lanes"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wave_intrinsic_helper_lane_hazard() {
    return std::make_unique<WaveIntrinsicHelperLaneHazard>();
}

}  // namespace shader_clippy::rules
