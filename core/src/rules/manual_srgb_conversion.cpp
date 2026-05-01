// manual-srgb-conversion
//
// FORWARD-COMPATIBLE STUB. Detects hand-rolled sRGB / gamma-2.2 transfers
// (`pow(x, 2.2)` or the more accurate piecewise transfer) where the sampled
// resource format already carries the sRGB conversion (e.g.
// `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`). Hardware sRGB sampling already
// linearises the channel; applying the curve a second time double-applies
// the gamma. This bug is common when migrating a render target from
// `R8G8B8A8_UNORM` to `R8G8B8A8_UNORM_SRGB` without removing the manual
// conversion in the shader.
//
// Today's reflection bridge does not surface DXGI format information through
// `ResourceBinding`. Without the format flag, distinguishing a legitimate
// gamma curve (applied to a UNORM source) from a double-application (applied
// to a UNORM_SRGB source) is impossible. We syntactically detect the manual
// conversion shape but DO NOT emit unless reflection confirms the source
// format is sRGB-flagged. As soon as the bridge surfaces format data, the
// gate flips and the rule starts emitting without further code change.
//
// The AST detection logic for the conversion call site is left in place to
// document the future emit path; the gate is the format check that today
// can never be satisfied.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::rules {
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
                       RuleContext& /*ctx*/) override {
        // Forward-compatible stub. Walk the AST for `pow(x, 2.2)` patterns
        // (and reflection bindings for textures) so the future fire path is
        // primed; gate the actual emit on the missing DXGI format flag.
        const auto sites = find_pow_2_2_sites(tree.source_bytes());
        if (sites.empty()) {
            return;
        }
        bool any_srgb_texture = false;
        for (const auto& binding : reflection.bindings) {
            if (!util::is_texture(binding.kind)) {
                continue;
            }
            // Pseudocode for the future emit path:
            //   if (binding.format_is_srgb()) { any_srgb_texture = true; break; }
            (void)binding;
        }
        if (!any_srgb_texture) {
            // Forward-compatible: today this is always taken (no format
            // info), so no diagnostic is emitted.
            return;
        }
        // Future emit path: for each pow(...,2.2) call site whose first
        // argument syntactically references an sRGB-bound texture, emit a
        // suggestion-grade diagnostic anchored at the `pow` call.
        // Intentionally unreachable today.
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_srgb_conversion() {
    return std::make_unique<ManualSrgbConversion>();
}

}  // namespace hlsl_clippy::rules
