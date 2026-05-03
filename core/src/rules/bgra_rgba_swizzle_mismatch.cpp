// bgra-rgba-swizzle-mismatch
//
// Detects shader reads of `.rgba` from a `Texture2D<float4>` whose binding
// maps a `DXGI_FORMAT_B8G8R8A8_UNORM` (BGRA) resource. Without an explicit
// `.bgra` swizzle on the read site, the shader silently inverts the red
// and blue channels -- a common real bug when IMGUI / UI pipelines mix
// swap-chain BGRA back-buffers with R8G8B8A8 SRGB material sampling.
//
// Fix grade (v1.2 -- ADR 0019, DXGI format reflection):
//   * machine-applicable when at least one bound texture's
//     `ResourceBinding::dxgi_format` actually contains "B8G8R8A8" (the
//     suffix DXGI uses for BGRA-channel-order formats). The fix is left
//     description-only because the rewrite point is the swizzle suffix on
//     the read site, which is not surfaced by reflection alone.
//   * Today's Slang ABI (2026.7.1) does NOT surface BGRA component-order
//     through `TypeReflection::getResourceResultType()` (the template arg
//     is `float4`, indistinguishable from RGBA), so `dxgi_format` is
//     "DXGI_FORMAT_R32G32B32A32_FLOAT" or empty for BGRA textures in
//     practice. The gate is therefore "primed" -- when a future Slang
//     surfaces the channel order, this rule auto-fires. Until then it
//     stays silent.

#include <memory>
#include <string>
#include <string_view>

#include "rules/util/reflect_resource.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "bgra-rgba-swizzle-mismatch";
constexpr std::string_view k_category = "texture";

class BgraRgbaSwizzleMismatch : public Rule {
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
        // v1.2 (ADR 0019): probe each texture binding's DXGI format for the
        // BGRA-channel-order suffix ("B8G8R8A8"). When at least one bound
        // texture is BGRA, surface a diagnostic anchored at the binding's
        // declaration so the user can audit the read sites.
        //
        // Today's Slang 2026.7.1 ABI doesn't surface BGRA component order
        // through reflection (the typed-resource template arg is `float4`,
        // indistinguishable from an RGBA channel layout), so
        // `dxgi_format` won't contain "B8G8R8A8" in practice -- the gate
        // stays unsatisfied and no diagnostic is emitted. The probe is
        // forward-compatible: when a future Slang surfaces channel order,
        // this rule lights up with zero further code change.
        for (const auto& binding : reflection.bindings) {
            if (!util::is_texture(binding.kind)) {
                continue;
            }
            if (binding.dxgi_format.find("B8G8R8A8") == std::string::npos) {
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span =
                Span{.source = tree.source_id(), .bytes = binding.declaration_span.bytes};
            diag.message = std::string{"binding `"} + binding.name +
                           "` is a `B8G8R8A8`-channel-order texture; reads using `.rgba` "
                           "silently invert the red and blue channels -- audit the read "
                           "sites and switch to `.bgra` (or rebind as `R8G8B8A8`)";

            Fix fix;
            // Description-only: the rewrite point is the swizzle suffix on
            // the .Sample() / .Load() site, which the reflection-stage
            // dispatch doesn't see directly. Future work: pair the format
            // probe with an AST query for `.rgba` post-fix on calls whose
            // receiver is `binding.name`, and surface a TextEdit for each
            // matched site.
            fix.machine_applicable = false;
            fix.description =
                std::string{
                    "switch the shader read swizzle from `.rgba` to `.bgra` on "
                    "every site that samples `"} +
                binding.name +
                "`, OR rebind the resource as `R8G8B8A8_UNORM` on the "
                "host side";
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_bgra_rgba_swizzle_mismatch() {
    return std::make_unique<BgraRgbaSwizzleMismatch>();
}

}  // namespace shader_clippy::rules
