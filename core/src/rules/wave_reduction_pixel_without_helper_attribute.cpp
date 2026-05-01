// wave-reduction-pixel-without-helper-attribute
//
// Detects a pixel-shader entry point that performs a wave reduction
// (`WaveActiveSum`, `WaveActiveProduct`, `WaveActiveCountBits`,
// `WaveActiveBallot`, `WaveActiveAllTrue`, `WaveActiveAnyTrue`,
// `WaveActiveBitOr`, etc.) whose result then flows into a derivative-bearing
// operation (`ddx`, `ddy`, an implicit-derivative `Sample(...)`) without
// `[WaveOpsIncludeHelperLanes]` declared on the entry. Per ADR 0010 §Phase 4
// (rule #26) and the SM 6.7 spec, the default mode excludes helper lanes
// from wave intrinsics; if the reduction's result then participates in a
// derivative, the helpers carry the wrong value and the derivative is
// contaminated -- visible as wrong mip levels on real GPUs.
//
// Stage: ControlFlow (forward-compatible-stub for Phase 4 reduction-to-
// derivative taint).
//
// The full rule needs a Phase 4 taint pass that propagates reach from any
// `WaveActive*` call through arithmetic / locals to a derivative-bearing
// op. Sub-phase 4b's uniformity oracle does not yet expose that taint sink,
// so this stub fires when an entry is a pixel shader (best-effort detection
// via `[shader("pixel")]` or a `: SV_Target*` return semantic) AND the
// function body textually contains both a `WaveActive*` call and a
// derivative consumer (`ddx`, `ddy`, `.Sample(`) AND the entry lacks the
// `[WaveOpsIncludeHelperLanes]` attribute. The taint analyzer will replace
// the textual co-occurrence with a real flow query.

#include <array>
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

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "wave-reduction-pixel-without-helper-attribute";
constexpr std::string_view k_category = "wave-helper-lane";
constexpr std::string_view k_attribute = "WaveOpsIncludeHelperLanes";

constexpr std::array<std::string_view, 9> k_wave_reductions{
    "WaveActiveSum",
    "WaveActiveProduct",
    "WaveActiveBitOr",
    "WaveActiveBitAnd",
    "WaveActiveBitXor",
    "WaveActiveCountBits",
    "WaveActiveBallot",
    "WaveActiveAllTrue",
    "WaveActiveAnyTrue",
};

constexpr std::array<std::string_view, 3> k_derivative_consumers{
    "ddx",
    "ddy",
    ".Sample(",  // implicit-derivative texture sample
};

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

/// True when `fn_text` looks like a pixel-shader entry: either an explicit
/// `[shader("pixel")]` attribute or a `SV_Target` return semantic. False
/// negatives are acceptable -- the rule remains silent on misclassified
/// stages, and the stage tag tightens once 4a's CFG carries stage metadata.
[[nodiscard]] bool looks_like_pixel_shader(std::string_view fn_text) noexcept {
    if (fn_text.find("[shader(\"pixel\")]") != std::string_view::npos) {
        return true;
    }
    // `: SV_TargetN` return semantic at function signature -- restrict to
    // before the first `{` to avoid matching SV_Target inside the body.
    const auto body = fn_text.find('{');
    const auto sig = (body == std::string_view::npos) ? fn_text : fn_text.substr(0, body);
    if (sig.find("SV_Target") != std::string_view::npos ||
        sig.find("SV_TARGET") != std::string_view::npos) {
        return true;
    }
    return false;
}

[[nodiscard]] bool contains_any(std::string_view text,
                                std::initializer_list<std::string_view> needles) noexcept {
    for (const auto needle : needles) {
        if (text.find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool contains_wave_reduction(std::string_view text) noexcept {
    for (const auto name : k_wave_reductions) {
        if (text.find(name) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool contains_derivative_consumer(std::string_view text) noexcept {
    for (const auto name : k_derivative_consumers) {
        if (text.find(name) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        if (looks_like_pixel_shader(fn_text) && !contains_any(fn_text, {k_attribute}) &&
            contains_wave_reduction(fn_text) && contains_derivative_consumer(fn_text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "pixel-shader entry mixes a `WaveActive*` reduction with a "
                "derivative-bearing op (`ddx`/`ddy`/`Sample`); helper lanes "
                "are excluded from the reduction by default and contaminate "
                "the derivative -- add `[WaveOpsIncludeHelperLanes]` (SM 6.7)"};
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class WaveReductionPixelWithoutHelperAttribute : public Rule {
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

    void on_cfg(const AstTree& tree, const ControlFlowInfo& /*cfg*/, RuleContext& ctx) override {
        // Forward-compatible: the textual co-occurrence shape is the
        // safe-and-loud approximation; the Phase 4 taint analyzer replaces
        // it with a real flow check from reduction result to derivative.
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_wave_reduction_pixel_without_helper_attribute() {
    return std::make_unique<WaveReductionPixelWithoutHelperAttribute>();
}

}  // namespace hlsl_clippy::rules
