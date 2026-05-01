// bgra-rgba-swizzle-mismatch
//
// FORWARD-COMPATIBLE STUB. Detects shader reads of `.rgba` from a
// `Texture2D<float4>` whose binding maps a `DXGI_FORMAT_B8G8R8A8_UNORM`
// (BGRA) resource. Without an explicit `.bgra` swizzle on the read site, the
// shader silently inverts the red and blue channels -- a common real bug
// when IMGUI / UI pipelines mix swap-chain BGRA back-buffers with R8G8B8A8
// SRGB material sampling.
//
// Today's reflection bridge does not surface DXGI format information through
// `ResourceBinding`; the binding's `kind` field tops out at the resource
// shape (`Texture2D` etc.), not its DXGI format. This rule's emit is gated
// behind a format check that is currently impossible to satisfy, so the rule
// is wired up but never fires. As soon as the bridge surfaces format data
// (planned for ADR 0012's reflection-format follow-up), the gate flips and
// the rule starts emitting without any further code change.
//
// AST detection of the syntactic pattern (`<binding>.Sample(...).rgba`) is
// intentionally NOT performed: without format info, every Texture2D<float4>
// read with a `.rgba` swizzle would fire (the whole point of using float4 is
// to read all four channels), producing 100% false positives. The forward-
// compatible stub deliberately emits nothing today rather than ship a noisy
// rule.

#include <memory>
#include <string>
#include <string_view>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules {
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

    void on_reflection(const AstTree& /*tree*/,
                       const ReflectionInfo& reflection,
                       RuleContext& /*ctx*/) override {
        // Forward-compatible stub. The fire condition is "binding's DXGI
        // format is B8G8R8A8_UNORM AND the read site uses .rgba swizzle".
        // Today the ResourceBinding struct does not carry DXGI format
        // information, so the loop walks bindings only to verify the rule is
        // observably wired (and so future maintenance touches the right
        // entry point); no diagnostic is emitted today.
        for (const auto& binding : reflection.bindings) {
            if (!util::is_texture(binding.kind)) {
                continue;
            }
            // Pseudocode for the future emit path:
            //   if (binding.format == DXGI_FORMAT_B8G8R8A8_UNORM &&
            //       read_site_uses_rgba_swizzle(tree, binding.name) &&
            //       !read_site_uses_bgra_swizzle(tree, binding.name)) {
            //     emit(...);
            //   }
            (void)binding;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_bgra_rgba_swizzle_mismatch() {
    return std::make_unique<BgraRgbaSwizzleMismatch>();
}

}  // namespace hlsl_clippy::rules
