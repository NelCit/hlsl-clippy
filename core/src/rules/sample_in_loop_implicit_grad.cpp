// sample-in-loop-implicit-grad
//
// Detects implicit-derivative `Texture.Sample` calls inside a loop. Implicit
// derivatives are computed across the 2x2 quad; if a lane's loop iteration
// count diverges from its quad neighbours, the derivative is undefined.
// Switching to `SampleLevel` / `SampleGrad` makes the LOD explicit and
// avoids the cross-lane derivative dependency.
//
// Stage: ControlFlow. Uses `cfg_query::inside_loop` to gate the firing.

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
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "sample-in-loop-implicit-grad";
constexpr std::string_view k_category = "control-flow";

void collect_implicit_samples(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        // Match `<recv>.Sample` exactly (not SampleLevel / SampleGrad / SampleCmpLevelZero).
        if (fn_text.size() > 7 && fn_text.ends_with(".Sample")) {
            out.push_back(node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_implicit_samples(::ts_node_child(node, i), bytes, out);
    }
}

class SampleInLoopImplicitGrad : public Rule {
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
        collect_implicit_samples(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_loop(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "implicit-derivative `Sample` inside a loop -- if the loop "
                "iteration count diverges across the 2x2 quad, the derivative "
                "is undefined; use `SampleLevel` / `SampleGrad` to make the "
                "LOD explicit"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "switch to `SampleLevel(samp, uv, lod)` or `SampleGrad(samp, "
                "uv, ddx_uv, ddy_uv)` -- the explicit LOD avoids the cross-"
                "lane derivative computation"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_sample_in_loop_implicit_grad() {
    return std::make_unique<SampleInLoopImplicitGrad>();
}

}  // namespace shader_clippy::rules
