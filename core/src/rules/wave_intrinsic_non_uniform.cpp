// wave-intrinsic-non-uniform
//
// Detects `WaveActive*` / `WavePrefix*` / `WaveAll*` / `WaveBallot` etc.
// calls inside non-uniform control flow. Wave intrinsics dispatched from a
// divergent region only reduce over the active lanes; this is rarely the
// caller's intent and is a frequent source of correctness bugs in scan /
// reduction kernels.
//
// Stage: ControlFlow. Uses `cfg_query::inside_divergent_cf` to gate the
// firing.

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
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "wave-intrinsic-non-uniform";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 18> k_wave_names{
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
    "WavePrefixCountBits",
    "WaveReadLaneAt",
    "WaveReadLaneFirst",
    "WaveMatch",
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

class WaveIntrinsicNonUniform : public Rule {
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
        collect_wave_calls(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_divergent_cf(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "wave intrinsic dispatched from non-uniform control flow -- "
                "the reduction only counts active lanes; this is rarely "
                "intended and a frequent source of scan / reduction bugs"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "hoist the wave intrinsic above the divergent branch, or "
                "ensure every lane in the wave reaches it (e.g. compute the "
                "value unconditionally, then select with the branch result)"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wave_intrinsic_non_uniform() {
    return std::make_unique<WaveIntrinsicNonUniform>();
}

}  // namespace hlsl_clippy::rules
