// vgpr-pressure-warning
//
// Source-level VGPR pressure heuristic. Per ADR 0017 §"Sub-phase 7c", this
// rule wraps `estimate_pressure(cfg, liveness, reflection)` and fires for
// every CFG block whose estimate exceeds a configurable threshold (default
// 64 -- approx 2x a wave's full-width VGPRs).
//
// Stage: ControlFlow. The warning is HEURISTIC -- AST-level liveness
// over-counts compared to what the actual register allocator emits. The
// rule ships at warn severity per ADR 0016 §"Best-effort precision".

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "control_flow/cfg_storage.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/liveness.hpp"
#include "rules/util/register_pressure_ast.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "vgpr-pressure-warning";
constexpr std::string_view k_category = "control-flow";
/// Default threshold matches `LintOptions::vgpr_pressure_threshold` default
/// (~RDNA wave32 x 2). Rules cannot read `LintOptions` directly today; the
/// default lives here and is overridable per-source via `.shader-clippy.toml`
/// in a v0.7.x patch.
constexpr std::uint32_t k_default_threshold = 64U;

class VgprPressureWarning : public Rule {
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
        const auto liveness = shader_clippy::util::compute_liveness(cfg, tree);
        if (liveness.live_in_per_block.empty())
            return;
        const auto pressure =
            shader_clippy::util::estimate_pressure(cfg, liveness, tree, nullptr, k_default_threshold);
        if (pressure.empty())
            return;

        // Worst-case block first (the helper sorts descending). Anchor the
        // diagnostic at the entry span of the worst block.
        const auto& worst = pressure.front();
        if (worst.estimated_vgprs <= k_default_threshold)
            return;

        // Walk the engine-internal storage to recover a span for the block.
        Span block_span;
        if (cfg.cfg.impl != nullptr) {
            const auto& storage = cfg.cfg.impl->data.storage;
            if (storage != nullptr) {
                for (const auto& [span, raw] : storage->span_to_block) {
                    if (raw == worst.block.raw()) {
                        block_span = span;
                        break;
                    }
                }
            }
        }
        if (block_span.bytes.empty()) {
            // Fall back to the CFG's recorded entry span.
            block_span = cfg.cfg.entry_span;
        }
        if (block_span.bytes.empty())
            return;

        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = block_span;
        diag.message = std::string{"basic block holds an estimated "} +
                       std::to_string(worst.estimated_vgprs) +
                       " VGPRs live (heuristic over AST-level liveness; > " +
                       std::to_string(k_default_threshold) +
                       " threshold) -- on RDNA / Turing+ / Xe-HPG this caps wave occupancy and "
                       "likely forces register spills to scratch; consider scoping locals more "
                       "tightly or restructuring the kernel into smaller phases";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_vgpr_pressure_warning() {
    return std::make_unique<VgprPressureWarning>();
}

}  // namespace shader_clippy::rules
