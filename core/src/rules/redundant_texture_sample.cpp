// redundant-texture-sample
//
// Detects two `<tex>.Sample(<sampler>, <uv>)` calls with byte-equal source
// spans for the texture, sampler, and UV expression within the SAME basic
// block. CSE across basic-block boundaries is fragile (the optimizer may
// not prove identity through phi merges), so within a single block any
// duplicate sample is wasted texture-fetch bandwidth on every IHV.
//
// Stage: ControlFlow. The CFG provides the per-block scoping; the rule
// pairs Sample calls within the block enclosing each one.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

constexpr std::string_view k_rule_id = "redundant-texture-sample";
constexpr std::string_view k_category = "control-flow";

[[nodiscard]] bool is_sample_callee(std::string_view fn_text) noexcept {
    static constexpr std::array<std::string_view, 4> k_methods{
        ".Sample",
        ".SampleLevel",
        ".SampleGrad",
        ".SampleBias",
    };
    for (const auto m : k_methods) {
        const auto pos = fn_text.rfind(m);
        if (pos != std::string_view::npos && pos + m.size() == fn_text.size()) {
            return true;
        }
    }
    return false;
}

void collect_sample_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (is_sample_callee(fn_text)) {
            out.push_back(node);
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        collect_sample_calls(::ts_node_child(node, i), bytes, out);
    }
}

class RedundantTextureSample : public Rule {
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
        collect_sample_calls(::ts_tree_root_node(tree.raw_tree()), bytes, calls);
        if (calls.size() < 2U)
            return;

        // Group by block id.
        struct Sample {
            ::TSNode call;
            std::string fingerprint;
        };
        std::unordered_map<std::uint32_t, std::vector<Sample>> per_block;
        for (const auto call : calls) {
            const auto call_span = Span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(call),
            };
            const auto block = rules::util::block_for(cfg, call_span);
            if (!block.has_value())
                continue;
            // Fingerprint = receiver text + sampler arg text + uv arg text.
            const auto fn = ::ts_node_child_by_field_name(call, "function", 8);
            std::string_view receiver{};
            if (!::ts_node_is_null(fn) && node_kind(fn) == "field_expression") {
                const auto recv = ::ts_node_child_by_field_name(fn, "argument", 8);
                receiver = node_text(recv, bytes);
            }
            const auto args = ::ts_node_child_by_field_name(call, "arguments", 9);
            if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) < 2U)
                continue;
            const auto sampler_arg = node_text(::ts_node_named_child(args, 0), bytes);
            const auto uv_arg = node_text(::ts_node_named_child(args, 1), bytes);
            std::string fp;
            fp.reserve(receiver.size() + sampler_arg.size() + uv_arg.size() + 4U);
            fp.append(receiver);
            fp.push_back('|');
            fp.append(sampler_arg);
            fp.push_back('|');
            fp.append(uv_arg);
            per_block[block->raw()].push_back(Sample{.call = call, .fingerprint = std::move(fp)});
        }

        for (auto& [block, samples] : per_block) {
            if (samples.size() < 2U)
                continue;
            // Pair-wise: emit a diagnostic on the SECOND sample of any
            // matched pair.
            std::vector<bool> reported(samples.size(), false);
            for (std::size_t i = 0; i < samples.size(); ++i) {
                for (std::size_t j = i + 1U; j < samples.size(); ++j) {
                    if (reported[j])
                        continue;
                    if (samples[i].fingerprint == samples[j].fingerprint &&
                        !samples[i].fingerprint.empty()) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{
                            .source = tree.source_id(),
                            .bytes = tree.byte_range(samples[j].call),
                        };
                        diag.message = std::string{
                            "duplicate texture sample within the same basic block "
                            "(matching texture, sampler, and UV expression) -- the second "
                            "sample is wasted bandwidth; CSE the result into a local"};
                        ctx.emit(std::move(diag));
                        reported[j] = true;
                    }
                }
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_texture_sample() {
    return std::make_unique<RedundantTextureSample>();
}

}  // namespace shader_clippy::rules
