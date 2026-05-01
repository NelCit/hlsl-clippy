// barrier-in-divergent-cf
//
// Detects `GroupMemoryBarrier*` / `DeviceMemoryBarrier*` / `AllMemoryBarrier*`
// calls inside non-uniform control flow. Barriers in divergent CF are
// undefined behaviour per the HLSL spec -- some lanes never reach the barrier
// and the workgroup deadlocks.
//
// Stage: ControlFlow. Uses `cfg_query::inside_divergent_cf`.

#include <array>
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
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "barrier-in-divergent-cf";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 6> k_barrier_names{
    "GroupMemoryBarrier",
    "GroupMemoryBarrierWithGroupSync",
    "DeviceMemoryBarrier",
    "DeviceMemoryBarrierWithGroupSync",
    "AllMemoryBarrier",
    "AllMemoryBarrierWithGroupSync",
};

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

void collect_barrier_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        for (const auto name : k_barrier_names) {
            if (fn_text == name) {
                out.push_back(node);
                break;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_barrier_calls(::ts_node_child(node, i), bytes, out);
    }
}

class BarrierInDivergentCf : public Rule {
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
        std::vector<::TSNode> calls;
        collect_barrier_calls(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_divergent_cf(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = span;
            diag.message = std::string{
                "memory barrier inside non-uniform control flow -- this is "
                "undefined behaviour; lanes that never reach the barrier "
                "deadlock the workgroup"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "hoist the barrier outside every divergent branch -- every "
                "lane must reach the barrier; if a lane needs to skip, "
                "restructure to keep the barrier on the join path"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_barrier_in_divergent_cf() {
    return std::make_unique<BarrierInDivergentCf>();
}

}  // namespace hlsl_clippy::rules
