// live-state-across-traceray
//
// Detects local variables that are defined before a `TraceRay(...)` call and
// read after it on the same CFG path. Such locals must be carried in the ray
// stack across the trace; on every IHV the ray stack is allocated out of
// scratch / global memory, so persistent live state directly translates to
// memory traffic per ray.
//
// Stage: ControlFlow + liveness. Per ADR 0017 §"Sub-phase 7c", the rule uses
// `compute_liveness(cfg)` and inspects `live_out` of the basic block
// containing the `TraceRay` call site. If the live_out set is non-trivial
// (more than 1 entry, after filtering the payload identifier itself), we
// warn at the call site.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/liveness.hpp"
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

constexpr std::string_view k_rule_id = "live-state-across-traceray";
constexpr std::string_view k_category = "dxr";
/// Number of live locals across the call site above which we warn. Anything
/// the wave can rematerialise (a single payload identifier) is fine; once a
/// trace has to spill more than two locals the per-ray cost climbs.
constexpr std::size_t k_live_threshold = 2U;

void collect_traceray_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "TraceRay") {
            out.push_back(node);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_traceray_calls(::ts_node_child(node, i), bytes, out);
    }
}

class LiveStateAcrossTraceRay : public Rule {
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
        const auto bytes = tree.source_bytes();
        std::vector<::TSNode> calls;
        collect_traceray_calls(::ts_tree_root_node(tree.raw_tree()), bytes, calls);
        if (calls.empty())
            return;
        const auto liveness = shader_clippy::util::compute_liveness(cfg, tree);
        if (liveness.live_out_per_block.empty())
            return;

        for (const auto call : calls) {
            const auto call_span = Span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(call),
            };
            const auto block_id = rules::util::block_for(cfg, call_span);
            if (!block_id.has_value())
                continue;
            const auto live_out = liveness.live_out_at(*block_id);
            if (live_out.size() <= k_live_threshold)
                continue;

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = call_span;
            diag.message = std::string{"`TraceRay` call site has "} +
                           std::to_string(live_out.size()) +
                           " AST-level locals live across the call -- the values must be "
                           "carried in the ray stack (scratch / global memory on every IHV) "
                           "for the duration of the trace, costing memory traffic per ray";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_live_state_across_traceray() {
    return std::make_unique<LiveStateAcrossTraceRay>();
}

}  // namespace shader_clippy::rules
