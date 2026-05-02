// maybereorderthread-without-payload-shrink
//
// Detects `MaybeReorderThread(...)` (SM 6.9 SER) call sites that still carry
// >= 4 locals live across the reorder. The Shader Execution Reordering
// scheduler can re-coalesce divergent waves only by rebuilding their state;
// values that the wave keeps live across the reorder must be moved with the
// thread, defeating the throughput win the SER intrinsic exists to deliver.
//
// Stage: ControlFlow + liveness. Mirrors the live-state-across-traceray rule
// but anchored at `MaybeReorderThread(...)` calls.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "rules/util/liveness.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "maybereorderthread-without-payload-shrink";
constexpr std::string_view k_category = "ser";
constexpr std::size_t k_live_threshold = 3U;

void collect_reorder_calls(::TSNode node,
                           std::string_view bytes,
                           std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        // Match either bare `MaybeReorderThread` or `dx::MaybeReorderThread`.
        if (fn_text == "MaybeReorderThread" || fn_text == "dx::MaybeReorderThread" ||
            (fn_text.size() >= 19U &&
             fn_text.substr(fn_text.size() - 19U) == "MaybeReorderThread")) {
            out.push_back(node);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_reorder_calls(::ts_node_child(node, i), bytes, out);
    }
}

class MaybeReorderThreadWithoutPayloadShrink : public Rule {
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
        collect_reorder_calls(::ts_tree_root_node(tree.raw_tree()), bytes, calls);
        if (calls.empty())
            return;
        const auto liveness = hlsl_clippy::util::compute_liveness(cfg, tree);
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
            diag.message = std::string{
                                "`MaybeReorderThread` call site has "} +
                            std::to_string(live_out.size()) +
                            " AST-level locals live across the reorder -- the SER scheduler "
                            "must move that state with the thread when it re-coalesces, "
                            "negating the throughput win the reorder is designed to deliver";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_maybereorderthread_without_payload_shrink() {
    return std::make_unique<MaybeReorderThreadWithoutPayloadShrink>();
}

}  // namespace hlsl_clippy::rules
