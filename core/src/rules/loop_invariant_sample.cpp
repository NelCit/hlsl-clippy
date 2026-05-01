// loop-invariant-sample
//
// Detects `Texture.Sample*` calls inside a loop where the UV / location
// argument is loop-invariant. The sample produces the same result on every
// iteration; hoisting it out of the loop saves the texture-fetch cost
// per iteration.
//
// Stage: ControlFlow. Uses `cfg_query::inside_loop` and
// `light_dataflow::loop_invariant_expr` to gate the firing.

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
#include "rules/util/light_dataflow.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "loop-invariant-sample";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 5> k_sample_names{
    "Sample",
    "SampleLevel",
    "SampleGrad",
    "SampleBias",
    "SampleCmp",
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

void collect_sample_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        // Match `<recv>.Sample*` patterns by checking the suffix.
        for (const auto name : k_sample_names) {
            const auto pos = fn_text.rfind(name);
            if (pos != std::string_view::npos && pos + name.size() == fn_text.size() && pos > 0 &&
                fn_text[pos - 1] == '.') {
                out.push_back(node);
                break;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_sample_calls(::ts_node_child(node, i), bytes, out);
    }
}

class LoopInvariantSample : public Rule {
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
        std::vector<::TSNode> sample_calls;
        collect_sample_calls(
            ::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), sample_calls);
        for (const auto call : sample_calls) {
            const auto call_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::inside_loop(cfg, call_span))
                continue;
            // Inspect the second positional argument (the UV / location); the
            // first is the sampler. If the UV expression is loop-invariant,
            // the whole sample is.
            const auto args = ::ts_node_child_by_field_name(call, "arguments", 9);
            if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) < 2U)
                continue;
            const auto uv = ::ts_node_named_child(args, 1);
            const auto uv_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(uv)};
            if (!util::loop_invariant_expr(cfg, uv_span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = call_span;
            diag.message = std::string{
                "texture sample inside a loop with loop-invariant UV -- the "
                "fetch returns the same value on every iteration; hoist it "
                "out of the loop"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "lift the sample to a block-scope local before the loop and "
                "reuse it inside; saves one texture fetch per iteration on "
                "every IHV"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_loop_invariant_sample() {
    return std::make_unique<LoopInvariantSample>();
}

}  // namespace hlsl_clippy::rules
