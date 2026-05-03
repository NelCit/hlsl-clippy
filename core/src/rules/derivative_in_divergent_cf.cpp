// derivative-in-divergent-cf
//
// Detects `ddx`/`ddy` / implicit-gradient `Texture.Sample` calls inside
// non-uniform control flow. Derivatives in the pixel shader are computed
// across the 2x2 quad; if even one quad lane has diverged, the derivative
// is undefined per the SM 6.x spec.
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

#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
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

constexpr std::string_view k_rule_id = "derivative-in-divergent-cf";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 6> k_derivative_intrinsics{
    "ddx",
    "ddy",
    "ddx_fine",
    "ddy_fine",
    "ddx_coarse",
    "ddy_coarse",
};

void collect_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        for (const auto name : k_derivative_intrinsics) {
            if (fn_text == name) {
                out.push_back(node);
                break;
            }
        }
        // Also flag implicit-gradient .Sample calls (no SampleLevel/SampleGrad).
        if (fn_text.size() > 7 && fn_text.ends_with(".Sample")) {
            out.push_back(node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_calls(::ts_node_child(node, i), bytes, out);
    }
}

class DerivativeInDivergentCf : public Rule {
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
        collect_calls(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_divergent_cf(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "derivative or implicit-gradient sample inside non-uniform "
                "control flow -- per the SM 6.x spec, derivatives across a "
                "diverged quad are undefined"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "hoist the derivative / implicit-Sample out of the divergent "
                "branch (compute it before, then use the value inside), or "
                "switch to SampleLevel/SampleGrad with explicit gradients"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_derivative_in_divergent_cf() {
    return std::make_unique<DerivativeInDivergentCf>();
}

}  // namespace shader_clippy::rules
