// clip-from-non-uniform-cf
//
// Companion of `early-z-disabled-by-conditional-discard` -- same hazard, but
// for `clip(x)` rather than `discard`. Detects a `clip(...)` call inside a
// pixel shader whose entry-point function is missing the
// `[earlydepthstencil]` attribute, when the path from entry to the clip
// passes through at least one branch with a non-uniform condition.
//
// Stage: ControlFlow.
//
// Detection plan:
//   1. Walk the AST for `call_expression` nodes whose function identifier is
//      `clip`.
//   2. For each match, look up the enclosing function's text and check
//      whether `[earlydepthstencil]` is present.
//   3. Use `cfg_query::reachable_with_discard` (which the engine treats as
//      `discard OR clip` per the helper-lane analyzer's design contract;
//      see `helper_lane_analyzer.hpp` -- "the engine records per-block
//      `contains_discard` flags ... `discard` (or the older `clip(...)`
//      form)") to confirm a discard/clip is reachable on the inbound path,
//      and `cfg_query::inside_divergent_cf` to confirm divergent control
//      flow at the call site.
//   4. Emit on the clip-call span when both conditions hold.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

constexpr std::string_view k_rule_id = "clip-from-non-uniform-cf";
constexpr std::string_view k_category = "control-flow";
constexpr std::string_view k_clip = "clip";
constexpr std::string_view k_earlyz = "[earlydepthstencil]";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

/// Walk parents from `node` up to the nearest enclosing `function_definition`
/// node. Returns a null `TSNode` when none exists.
[[nodiscard]] ::TSNode enclosing_function(::TSNode node) noexcept {
    auto cur = ::ts_node_parent(node);
    while (!::ts_node_is_null(cur)) {
        const char* t = ::ts_node_type(cur);
        if (t != nullptr && std::string_view{t} == "function_definition") {
            return cur;
        }
        cur = ::ts_node_parent(cur);
    }
    return ::TSNode{};
}

/// True when `text` contains the substring `[earlydepthstencil]` (with
/// optional whitespace inside the brackets) before any `{` opening brace.
[[nodiscard]] bool function_has_earlyz(std::string_view fn_text) noexcept {
    const auto brace = fn_text.find('{');
    const auto search_in = (brace == std::string_view::npos) ? fn_text : fn_text.substr(0, brace);
    return search_in.find(k_earlyz) != std::string_view::npos;
}

void walk(::TSNode node,
          std::string_view bytes,
          const AstTree& tree,
          const ControlFlowInfo& cfg,
          RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "call_expression") {
        const auto fn_field = ::ts_node_child_by_field_name(node, "function", 8U);
        const auto fn_name = node_text(fn_field, bytes);
        if (fn_name == k_clip) {
            const auto enclosing = enclosing_function(node);
            const auto fn_text = node_text(enclosing, bytes);
            const bool already_earlyz = function_has_earlyz(fn_text);
            if (!already_earlyz) {
                const auto call_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
                const auto call_hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
                const Span call_span{
                    .source = tree.source_id(),
                    .bytes = ByteSpan{.lo = call_lo, .hi = call_hi},
                };
                // Treat clip-as-discard for reachability + inside-divergent-CF
                // tests: the engine's `contains_discard` flag covers both
                // (per helper_lane_analyzer.hpp design comment).
                const bool inside_div = util::inside_divergent_cf(cfg, call_span);
                const bool reachable_clip = util::reachable_with_discard(cfg, call_span);
                if (inside_div || reachable_clip) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = call_span;
                    diag.message = std::string{
                        "`clip(...)` reached from non-uniform control flow without "
                        "`[earlydepthstencil]` -- early-Z is downgraded to late-Z for the "
                        "whole pipeline state, losing the bandwidth optimisation on every "
                        "fragment; either add `[earlydepthstencil]`, hoist the clip to a "
                        "wave-uniform predicate, or move the alpha test out of the shader"};
                    ctx.emit(std::move(diag));
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, cfg, ctx);
    }
}

class ClipFromNonUniformCf : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, cfg, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_clip_from_non_uniform_cf() {
    return std::make_unique<ClipFromNonUniformCf>();
}

}  // namespace hlsl_clippy::rules
