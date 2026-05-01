// discard-then-work
//
// Detects significant work performed after a `discard` statement in pixel
// shaders. Once a lane discards, it stays alive as a helper lane for
// derivative / quad-uniform purposes, but every cycle of work it performs is
// throwaway. The fix is to move work that does not feed derivatives to
// before the discard.
//
// Stage: ControlFlow. Uses `helper_lane_analyzer::possibly_helper_lane_at`
// to find program points reachable on a discard-passing path, plus a simple
// "non-trivial work" filter (any call_expression that is not itself a
// derivative / quad-uniform op).

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
#include "rules/util/helper_lane_analyzer.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "discard-then-work";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 8> k_helper_safe{
    "ddx",
    "ddy",
    "ddx_fine",
    "ddy_fine",
    "ddx_coarse",
    "ddy_coarse",
    "QuadAny",
    "QuadAll",
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

void collect_calls(::TSNode node, std::string_view bytes, std::vector<::TSNode>& out) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        bool helper_safe = false;
        for (const auto name : k_helper_safe) {
            if (fn_text == name) {
                helper_safe = true;
                break;
            }
        }
        if (!helper_safe) {
            out.push_back(node);
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_calls(::ts_node_child(node, i), bytes, out);
    }
}

class DiscardThenWork : public Rule {
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
        collect_calls(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), calls);
        for (const auto call : calls) {
            const auto span = Span{.source = tree.source_id(), .bytes = tree.byte_range(call)};
            if (!util::possibly_helper_lane_at(cfg, span))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = span;
            diag.message = std::string{
                "non-trivial work after a `discard` -- helper lanes still "
                "execute every instruction; move work that does not feed "
                "derivatives to before the discard"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "rearrange so the discard is the last statement (or at least "
                "follows every non-derivative computation); helper lanes pay "
                "for every instruction after the discard on every IHV"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_discard_then_work() {
    return std::make_unique<DiscardThenWork>();
}

}  // namespace hlsl_clippy::rules
