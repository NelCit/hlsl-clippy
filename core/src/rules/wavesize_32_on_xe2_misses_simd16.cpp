// wavesize-32-on-xe2-misses-simd16
//
// Detects `[WaveSize(32)]` declarations on compute kernels targeting Xe2 /
// Battlemage. Xe2 SIMD16 native execution saves one cycle of address-gen
// latency per dispatch over SIMD32, so a SIMD32-pinned kernel hides
// Battlemage native efficiency. Suggestion-grade -- the right wave size
// depends on the kernel's lane utilisation.
//
// Stage: Reflection. Gated behind `[experimental.target = xe2]`.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_stage.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "wavesize-32-on-xe2-misses-simd16";
constexpr std::string_view k_category = "xe2";

class WaveSize32OnXe2MissesSimd16 : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept override {
        return ExperimentalTarget::Xe2;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        for (const auto& ep : reflection.entry_points) {
            if (!util::is_compute_shader(ep) && !util::is_mesh_or_amp_shader(ep)) {
                continue;
            }
            const auto ws = util::wave_size_for_entry_point(tree, ep);
            if (ws.has_value() && ws->first == 32U && ws->second == 32U) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = ep.declaration_span;
                diag.message = std::string{"(suggestion) entry point `"} + ep.name +
                               "` is pinned `[WaveSize(32)]`; Xe2 / Battlemage SIMD16 "
                               "native execution saves one address-gen cycle per dispatch "
                               "over SIMD32 -- consider `[WaveSize(16)]` or `[WaveSize(16, 32)]` "
                               "if the kernel's lane utilisation allows";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wavesize_32_on_xe2_misses_simd16() {
    return std::make_unique<WaveSize32OnXe2MissesSimd16>();
}

}  // namespace shader_clippy::rules
