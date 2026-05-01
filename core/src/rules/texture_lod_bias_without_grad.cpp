// texture-lod-bias-without-grad
//
// Detects `<tex>.SampleBias(<sampler>, <uv>, <bias>)` calls inside compute
// or other non-quad-uniform contexts. `SampleBias` relies on implicit
// derivatives that only exist inside a 2x2 pixel quad; calling it from a
// compute shader (or any non-quad-uniform helper) is undefined behaviour
// per the HLSL spec.
//
// Detection plan: AST. Match `<id>.SampleBias(...)` calls; emit when the
// source contains a `[shader("compute")]` (or `[numthreads(...)]`) attribute
// that suggests we're not in a pixel shader. The check is intentionally
// coarse: a single `SampleBias` in a file with no pixel-shader entry point
// is suspect by itself. Suggestion-only.

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

constexpr std::string_view k_rule_id = "texture-lod-bias-without-grad";
constexpr std::string_view k_category = "texture";

class TextureLodBiasWithoutGrad : public Rule {
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
        // Look at the file globally for evidence of a compute / mesh / amp /
        // raytracing entry point. If only pixel-shader entry points are
        // present, suppress.
        const bool has_pixel = bytes.find("\"pixel\"") != std::string_view::npos;
        const bool has_non_pixel_compute =
            bytes.find("[numthreads") != std::string_view::npos ||
            bytes.find("\"compute\"") != std::string_view::npos ||
            bytes.find("\"mesh\"") != std::string_view::npos ||
            bytes.find("\"amplification\"") != std::string_view::npos;
        if (has_pixel && !has_non_pixel_compute)
            return;
        constexpr std::string_view k_needle = ".SampleBias(";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos)
                return;
            // `.SampleBias(` -- check it's a method call (preceded by id or `]`).
            // Find the call end.
            int depth = 0;
            std::size_t i = found + std::string_view{".SampleBias"}.size();
            if (i >= bytes.size()) {
                pos = found + 1U;
                continue;
            }
            while (i < bytes.size()) {
                if (bytes[i] == '(')
                    ++depth;
                else if (bytes[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
                ++i;
            }
            const auto call_lo = found + 1U;
            const auto call_hi = i;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(call_lo),
                                                       static_cast<std::uint32_t>(call_hi)}};
            diag.message = std::string{
                "`SampleBias` relies on implicit pixel-quad derivatives -- in compute / mesh / "
                "ray-tracing contexts there is no quad and the call is undefined behaviour; "
                "use `SampleGrad` with explicit gradients or `SampleLevel` with an explicit LOD"};
            ctx.emit(std::move(diag));
            pos = call_hi;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_texture_lod_bias_without_grad() {
    return std::make_unique<TextureLodBiasWithoutGrad>();
}

}  // namespace hlsl_clippy::rules
