// feedback-write-wrong-stage
//
// Detects `WriteSamplerFeedback*` calls in entry points that are not pixel
// shaders. The HLSL spec restricts sampler-feedback writes to PS; calling
// them from compute / mesh / amplification / raytracing stages is undefined
// behaviour at best, a PSO-creation failure at worst.
//
// Detection plan: AST + reflection. Use the reflection entry-point list to
// scope each `WriteSamplerFeedback*` call to its containing entry point.
// When the entry point's stage is not "pixel", emit. As a coarse
// approximation when reflection is not available, we also flag any
// `WriteSamplerFeedback*` in a file that contains a `[numthreads(...)]`
// attribute and no `[shader("pixel")]` attribute.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "feedback-write-wrong-stage";
constexpr std::string_view k_category = "sampler-feedback";

class FeedbackWriteWrongStage : public Rule {
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
        const auto bytes = tree.source_bytes();
        // If reflection reports any non-pixel entry point and the file
        // contains a `WriteSamplerFeedback*` call, flag it.
        bool has_non_pixel_ep = false;
        bool has_pixel_ep = false;
        for (const auto& ep : reflection.entry_points) {
            if (ep.stage == "pixel")
                has_pixel_ep = true;
            else
                has_non_pixel_ep = true;
        }
        if (!has_non_pixel_ep && reflection.entry_points.empty()) {
            // Reflection failed to surface entry points; fall back to
            // attribute scan.
            const bool has_compute = bytes.find("[numthreads") != std::string_view::npos;
            const bool has_pixel_attr = bytes.find("\"pixel\"") != std::string_view::npos;
            if (has_compute && !has_pixel_attr)
                has_non_pixel_ep = true;
        }
        if (!has_non_pixel_ep)
            return;
        constexpr std::string_view k_needle = "WriteSamplerFeedback";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos)
                return;
            const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
            if (!ok_left) {
                pos = found + 1U;
                continue;
            }
            // Read the full method name (could be WriteSamplerFeedback,
            // WriteSamplerFeedbackBias, etc.) then a `(`.
            std::size_t k = found + k_needle.size();
            while (k < bytes.size() && is_id_char(bytes[k]))
                ++k;
            if (k >= bytes.size() || bytes[k] != '(') {
                pos = found + 1U;
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                       static_cast<std::uint32_t>(k)}};
            diag.message = std::string{
                "`WriteSamplerFeedback*` is restricted to pixel shaders -- this file declares "
                "non-pixel entry points and the call may execute outside its supported stage"};
            // Suppress duplicate emit when the file is purely pixel.
            if (has_pixel_ep && !has_non_pixel_ep) {
                pos = k;
                continue;
            }
            ctx.emit(std::move(diag));
            pos = k;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_feedback_write_wrong_stage() {
    return std::make_unique<FeedbackWriteWrongStage>();
}

}  // namespace hlsl_clippy::rules
