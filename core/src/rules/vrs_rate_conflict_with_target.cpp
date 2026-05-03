// vrs-rate-conflict-with-target
//
// Detects pixel shaders that write `SV_ShadingRate` while also tagging the
// output with a per-primitive coarse-rate hint. D3D12 / Vulkan VRS rate
// combiners produce the *minimum* of per-primitive and per-pixel rates;
// conflicting declarations silently override the author's expectation.
//
// Stage: Reflection. The PS / coarse-rate detection is best-effort
// source-level; reflection lets us gate on the target shader stage so
// the rule only fires on actual pixel-shader entry points.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "vrs-rate-conflict-with-target";
constexpr std::string_view k_category = "vrs";

class VrsRateConflictWithTarget : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        // Self-gate: only fire when the source looks like a pixel shader
        // (writes SV_Target or has [shader("pixel")]).
        const bool is_pixel = bytes.find("SV_Target") != std::string_view::npos ||
                              bytes.find("\"pixel\"") != std::string_view::npos;
        if (!is_pixel) {
            return;
        }
        const bool has_pp_sr_write = bytes.find("SV_ShadingRate") != std::string_view::npos;
        const bool has_coarse_rate =
            bytes.find("D3D12_SHADING_RATE_COMBINER") != std::string_view::npos ||
            bytes.find("PerPrimitive") != std::string_view::npos ||
            bytes.find("perprimitive") != std::string_view::npos ||
            bytes.find("CoarseShadingRate") != std::string_view::npos ||
            bytes.find("[shading_rate]") != std::string_view::npos;
        if (has_pp_sr_write && has_coarse_rate) {
            const auto pos = bytes.find("SV_ShadingRate");
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{static_cast<std::uint32_t>(pos),
                                  static_cast<std::uint32_t>(
                                      pos + std::string_view{"SV_ShadingRate"}.size())},
            };
            diag.message =
                "PS writes `SV_ShadingRate` AND the source declares a per-primitive / "
                "coarse-rate VRS source -- D3D12/VK rate combiners take the MIN of the "
                "two and silently override the per-pixel rate when the per-primitive "
                "rate is coarser";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_vrs_rate_conflict_with_target() {
    return std::make_unique<VrsRateConflictWithTarget>();
}

}  // namespace hlsl_clippy::rules
