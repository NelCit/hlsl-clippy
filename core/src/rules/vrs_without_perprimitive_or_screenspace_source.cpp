// vrs-without-perprimitive-or-screenspace-source
//
// Detects PS entry points that emit `SV_ShadingRate` but have no upstream
// per-primitive or screen-space VRS source (e.g. no `[earlydepthstencil]`
// or matching upstream attribute). PS-emitted VRS rates without an upstream
// source are ignored on most IHVs.
//
// Stage: Reflection. We need to know we're in a PS to fire; the upstream
// markers are detected source-side as a best-effort heuristic.

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

constexpr std::string_view k_rule_id = "vrs-without-perprimitive-or-screenspace-source";
constexpr std::string_view k_category = "vrs";

class VrsWithoutPerPrimitiveOrScreenSpaceSource : public Rule {
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
        // Self-gate: pixel-shader-shaped source.
        const bool is_pixel = bytes.find("SV_Target") != std::string_view::npos ||
                              bytes.find("\"pixel\"") != std::string_view::npos;
        if (!is_pixel) {
            return;
        }
        const auto sr_pos = bytes.find("SV_ShadingRate");
        if (sr_pos == std::string_view::npos) {
            return;
        }
        // Look for any indicator of an upstream VRS source.
        const bool has_upstream_source =
            bytes.find("[earlydepthstencil]") != std::string_view::npos ||
            bytes.find("PerPrimitive") != std::string_view::npos ||
            bytes.find("perprimitive") != std::string_view::npos ||
            bytes.find("CoarseShadingRate") != std::string_view::npos ||
            bytes.find("ScreenSpaceShadingRate") != std::string_view::npos ||
            bytes.find("VK_FRAGMENT_SHADING_RATE") != std::string_view::npos;
        if (has_upstream_source) {
            return;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{
            .source = tree.source_id(),
            .bytes = ByteSpan{static_cast<std::uint32_t>(sr_pos),
                              static_cast<std::uint32_t>(sr_pos +
                                                        std::string_view{"SV_ShadingRate"}.size())},
        };
        diag.message =
            "PS emits `SV_ShadingRate` but no upstream per-primitive or "
            "screen-space VRS source is visible -- without an upstream source "
            "(`[earlydepthstencil]`, per-primitive coarse-rate, or screen-space "
            "VRS image) the PS-emitted rate is ignored on most IHVs";
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_vrs_without_perprimitive_or_screenspace_source() {
    return std::make_unique<VrsWithoutPerPrimitiveOrScreenSpaceSource>();
}

}  // namespace hlsl_clippy::rules
