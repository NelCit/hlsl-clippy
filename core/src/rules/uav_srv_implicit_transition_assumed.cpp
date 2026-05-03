// uav-srv-implicit-transition-assumed
//
// Detects a reflection-visible alias between a UAV binding `U` and an SRV
// binding `S`. D3D12 requires an explicit barrier between a write to a UAV
// and a subsequent read of the same underlying resource through an SRV view;
// when the binding shape suggests the two views may target the same resource,
// the application-side barrier code becomes load-bearing. Surfacing the
// alias from reflection is documentation-grade -- it tells the developer
// "audit the host-side barrier here" rather than asserting a bug.
//
// Detection (Reflection-stage):
//   For every pair of bindings (U, S) where:
//     - `U` is writable (UAV-like) per `is_writable(...)`,
//     - `S` is a non-writable buffer/texture (read-only resource),
//     - either:
//         (a) the two bindings share the same `register_slot` (in any space)
//             but have different `register_space` values; OR
//         (b) the two bindings have identical names but different kinds.
//   Emit one suggestion-grade diagnostic per alias pair anchored at the
//   UAV declaration (or the synthetic span if reflection didn't populate
//   one). No fix -- the linter cannot synthesise a host-side barrier.
//
// Stage: Reflection (uses ResourceBinding::register_slot/space + kind).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "uav-srv-implicit-transition-assumed";
constexpr std::string_view k_category = "bindings";

/// True when `kind` is a non-writable resource that can read from a
/// resource the application side may have just written through a UAV view.
/// We restrict to buffer + texture shapes (not samplers, not constant
/// buffers) since only those can plausibly alias a UAV.
[[nodiscard]] bool is_readable_view(ResourceKind kind) noexcept {
    if (util::is_writable(kind)) {
        return false;
    }
    if (kind == ResourceKind::SamplerState || kind == ResourceKind::SamplerComparisonState ||
        kind == ResourceKind::ConstantBuffer || kind == ResourceKind::AccelerationStructure) {
        return false;
    }
    return util::is_texture(kind) || util::is_buffer(kind);
}

class UavSrvImplicitTransitionAssumed : public Rule {
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

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        const auto& bindings = reflection.bindings;
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            const auto& uav = bindings[i];
            if (!util::is_writable(uav.kind)) {
                continue;
            }
            for (std::size_t j = 0; j < bindings.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const auto& srv = bindings[j];
                if (!is_readable_view(srv.kind)) {
                    continue;
                }
                // Alias condition (a): same register slot, different space.
                const bool aliased_by_slot = (uav.register_slot == srv.register_slot) &&
                                             (uav.register_space != srv.register_space);
                // Alias condition (b): identical names, different kinds.
                // (We already know the kinds differ -- one is writable and
                // the other is not -- so the name match alone is enough.)
                const bool aliased_by_name = (!uav.name.empty()) && (uav.name == srv.name);
                if (!aliased_by_slot && !aliased_by_name) {
                    continue;
                }

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                if (uav.declaration_span.bytes.hi > 0) {
                    diag.primary_span = uav.declaration_span;
                } else {
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = 0, .hi = 0}};
                }

                std::string reason;
                if (aliased_by_slot) {
                    reason = std::string{"share register slot u/t"} +
                             std::to_string(uav.register_slot) + std::string{" across spaces "} +
                             std::to_string(uav.register_space) + std::string{" vs "} +
                             std::to_string(srv.register_space);
                } else {
                    reason = std::string{"share the identifier name"};
                }

                diag.message = std::string{"UAV binding `"} + uav.name +
                               std::string{"` and SRV binding `"} + srv.name + std::string{"` "} +
                               reason +
                               std::string{
                                   " -- D3D12 requires an explicit UAV->SRV barrier "
                                   "between the write and the subsequent read; audit the "
                                   "host-side barrier code"};
                // Suggestion-grade: NO fix. The barrier lives in host code,
                // not in the shader.
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_uav_srv_implicit_transition_assumed() {
    return std::make_unique<UavSrvImplicitTransitionAssumed>();
}

}  // namespace shader_clippy::rules
