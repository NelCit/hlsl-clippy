// wave64-on-rdna4-compute-misses-dynamic-vgpr
//
// Detects compute kernels declared `[WaveSize(64)]` (or compute shaders
// with no [WaveSize] in a context that defaults to wave64). Per AMD's
// RDNA 4 deep-dives, dynamic-VGPR mode is wave32-only -- wave64 compute
// on RDNA 4 silently misses the occupancy gain.
//
// Stage: Reflection. Gated behind `[experimental.target = rdna4]`.

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

constexpr std::string_view k_rule_id = "wave64-on-rdna4-compute-misses-dynamic-vgpr";
constexpr std::string_view k_category = "rdna4";

class Wave64OnRdna4ComputeMissesDynamicVgpr : public Rule {
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
        return ExperimentalTarget::Rdna4;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        for (const auto& ep : reflection.entry_points) {
            if (!util::is_compute_shader(ep)) {
                continue;
            }
            const auto ws = util::wave_size_for_entry_point(tree, ep);
            // Fire when the entry point explicitly pins wave64.
            if (ws.has_value() && ws->first >= 64U) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = ep.declaration_span;
                diag.message = std::string{"compute entry point `"} + ep.name +
                               "` is declared `[WaveSize(64)]`; RDNA 4 dynamic-VGPR mode is "
                               "wave32-only, so wave64 compute on RDNA 4 silently misses the "
                               "per-block occupancy gain";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_wave64_on_rdna4_compute_misses_dynamic_vgpr() {
    return std::make_unique<Wave64OnRdna4ComputeMissesDynamicVgpr>();
}

}  // namespace shader_clippy::rules
