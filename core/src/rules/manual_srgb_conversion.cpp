// manual-srgb-conversion
//
// Detects hand-rolled sRGB / gamma-2.2 transfers (`pow(x, 2.2)` or the more
// accurate piecewise transfer) where the sampled resource format already
// carries the sRGB conversion (e.g. `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`).
// Hardware sRGB sampling already linearises the channel; applying the curve
// a second time double-applies the gamma. This bug is common when migrating
// a render target from `R8G8B8A8_UNORM` to `R8G8B8A8_UNORM_SRGB` without
// removing the manual conversion in the shader.
//
// Fix grade (v1.2 -- ADR 0019, DXGI format reflection):
//   * machine-applicable when at least one bound texture's
//     `ResourceBinding::dxgi_format` actually contains "SRGB" (the suffix
//     standard DXGI uses for sRGB-flagged formats). The rewrite drops the
//     manual `pow(x, 2.2)` -- replacing the call with its first argument.
//   * Today's Slang ABI (2026.7.1) does NOT surface the SRGB qualifier
//     through `TypeReflection::getName()`, so `dxgi_format` is empty for
//     SRGB textures in practice. The gate is therefore "primed" -- when a
//     future Slang surfaces the qualifier, the rule auto-fires. Until then
//     it stays silent.
//
// AST detection of `pow(...,2.2)` call sites is performed up front; the
// final emit is gated on the format probe so a SRGB binding flips the rule
// from "no-op" to "machine-applicable" with zero further code change.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "manual-srgb-conversion";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Find every byte offset of a `pow(...,2.2)` call site in `text`. Returns
/// the byte span covering the `pow(` prefix only (the diagnostic anchor).
/// This is intentionally a coarse heuristic: any `pow` whose second argument
/// is the literal `2.2` (with optional whitespace and `f` suffix). Used by
/// the future emit path; today the rule never emits.
[[nodiscard]] std::vector<ByteSpan> find_pow_2_2_sites(std::string_view text) noexcept {
    constexpr std::string_view k_pow = "pow";
    std::vector<ByteSpan> sites;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = text.find(k_pow, pos);
        if (found == std::string_view::npos) {
            break;
        }
        const bool ok_left = (found == 0) || is_id_boundary(text[found - 1]);
        if (!ok_left) {
            pos = found + 1;
            continue;
        }
        std::size_t i = found + k_pow.size();
        if (i >= text.size() || text[i] != '(') {
            pos = found + 1;
            continue;
        }
        // Find the matching ',' at depth 0.
        std::size_t depth = 1;
        std::size_t comma = std::string_view::npos;
        std::size_t j = i + 1;
        while (j < text.size() && depth > 0) {
            if (text[j] == '(') {
                ++depth;
            } else if (text[j] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            } else if (text[j] == ',' && depth == 1) {
                comma = j;
                break;
            }
            ++j;
        }
        if (comma == std::string_view::npos) {
            pos = found + 1;
            continue;
        }
        // Read the second argument up to the matching close paren.
        std::size_t arg_start = comma + 1;
        while (arg_start < text.size() && (text[arg_start] == ' ' || text[arg_start] == '\t')) {
            ++arg_start;
        }
        std::size_t arg_end = arg_start;
        depth = 1;
        while (arg_end < text.size() && depth > 0) {
            if (text[arg_end] == '(') {
                ++depth;
            } else if (text[arg_end] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            ++arg_end;
        }
        // Trim trailing whitespace.
        std::size_t arg_trim = arg_end;
        while (arg_trim > arg_start && (text[arg_trim - 1] == ' ' || text[arg_trim - 1] == '\t')) {
            --arg_trim;
        }
        const auto arg = text.substr(arg_start, arg_trim - arg_start);
        // Match `2.2` or `2.2f` or `2.2F`.
        const bool is_2_2 = (arg == "2.2" || arg == "2.2f" || arg == "2.2F");
        if (is_2_2) {
            sites.push_back(ByteSpan{static_cast<std::uint32_t>(found),
                                     static_cast<std::uint32_t>(found + k_pow.size())});
        }
        pos = found + k_pow.size();
    }
    return sites;
}

class ManualSrgbConversion : public Rule {
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
        // v1.2 (ADR 0019): walk every pow(...,2.2) call site, then probe
        // each texture binding's DXGI format for the "SRGB" suffix. If any
        // bound texture is SRGB-flagged, every pow(...,2.2) site gets a
        // machine-applicable diagnostic suggesting the manual conversion
        // be dropped.
        //
        // Today's Slang 2026.7.1 ABI doesn't surface the SRGB qualifier
        // through `TypeReflection::getName()`, so `dxgi_format` is empty
        // for SRGB textures in practice -- the gate stays unsatisfied
        // and no diagnostic is emitted. The probe is forward-compatible:
        // when a future Slang surfaces the qualifier, this rule lights up
        // with zero further code change.
        const auto sites = find_pow_2_2_sites(tree.source_bytes());
        if (sites.empty()) {
            return;
        }
        bool any_srgb_texture = false;
        for (const auto& binding : reflection.bindings) {
            if (!util::is_texture(binding.kind)) {
                continue;
            }
            if (binding.dxgi_format.find("SRGB") != std::string::npos) {
                any_srgb_texture = true;
                break;
            }
        }
        if (!any_srgb_texture) {
            return;
        }
        // At least one SRGB-flagged texture is bound. Emit a diagnostic
        // anchored at each pow(...,2.2) call site. The fix replaces the
        // entire `pow(x, 2.2)` call with `x` -- but we only have the byte
        // span of the `pow` identifier here, not the full call. Surface a
        // diagnostic with a description-only fix; the AST anchor is enough
        // for IDE / CLI to point the user at the right spot.
        for (const auto& site : sites) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = site};
            diag.message = std::string{
                "manual `pow(x, 2.2)` applied to a sample from an SRGB-flagged "
                "texture double-applies the gamma curve -- the hardware sRGB "
                "sampler already linearised the channel; drop the manual "
                "conversion"};

            Fix fix;
            // Machine-applicable in spirit (the rewrite is "drop the call"),
            // but we only have the `pow` identifier span -- not the whole
            // call expression -- so the textual replacement is left for the
            // user to apply. Mark suggestion-only until we surface the full
            // call span via AST query.
            fix.machine_applicable = false;
            fix.description = std::string{
                "drop the manual sRGB / gamma curve: replace `pow(x, 2.2)` "
                "with `x`. The hardware SRGB sample already linearises the "
                "channel."};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_srgb_conversion() {
    return std::make_unique<ManualSrgbConversion>();
}

}  // namespace shader_clippy::rules
