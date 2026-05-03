// sampler-feedback-without-streaming-flag
//
// Sampler feedback writes (`WriteSamplerFeedback*`) record texture-LOD
// access patterns into a feedback texture so a streaming system can
// upload only the mip levels actually demanded. If the shader emits the
// writes but the application has no tiled / reserved-resource binding
// visible in reflection, the feedback texture is dead bandwidth on every
// IHV that materialises it.
//
// Detection (Reflection stage):
//   1. AST scan: find every `WriteSamplerFeedback*` call (any of the
//      `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`,
//      `WriteSamplerFeedbackGrad`, `WriteSamplerFeedbackLevel` family).
//   2. Reflection scan: look for at least one `FeedbackTexture2D` /
//      `FeedbackTexture2DArray` binding -- those are the binding kinds
//      sampler-feedback writes must target. (A complete check would also
//      verify the application binds a tiled paired resource, but the
//      reflection surface only exposes shader-visible bindings; the
//      feedback-resource-kind check is the strongest signal we have.)
//   3. If WriteSamplerFeedback* calls are present AND no feedback-texture
//      binding is visible in reflection, emit one diagnostic per call site.
//
// Suggestion-grade; no machine-applicable fix.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "sampler-feedback-without-streaming-flag";
constexpr std::string_view k_category = "sampler-feedback";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Find every byte offset in `text` where a complete identifier whose name
/// begins with `WriteSamplerFeedback` appears.
[[nodiscard]] std::vector<ByteSpan> find_call_sites(std::string_view text) noexcept {
    constexpr std::string_view k_prefix = "WriteSamplerFeedback";
    std::vector<ByteSpan> sites;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = text.find(k_prefix, pos);
        if (found == std::string_view::npos) {
            break;
        }
        const bool ok_left = (found == 0) || is_id_boundary(text[found - 1]);
        if (ok_left) {
            // Walk to the end of the identifier (any trailing letters are
            // part of the call name, e.g. `WriteSamplerFeedbackLevel`).
            std::size_t end = found + k_prefix.size();
            while (end < text.size() && ((text[end] >= 'a' && text[end] <= 'z') ||
                                         (text[end] >= 'A' && text[end] <= 'Z'))) {
                ++end;
            }
            sites.push_back(
                ByteSpan{static_cast<std::uint32_t>(found), static_cast<std::uint32_t>(end)});
            pos = end;
            continue;
        }
        pos = found + 1;
    }
    return sites;
}

class SamplerFeedbackWithoutStreamingFlag : public Rule {
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
        const auto sites = find_call_sites(bytes);
        if (sites.empty()) {
            return;
        }

        bool has_feedback_binding = false;
        for (const auto& binding : reflection.bindings) {
            if (binding.kind == ResourceKind::FeedbackTexture2D ||
                binding.kind == ResourceKind::FeedbackTexture2DArray) {
                has_feedback_binding = true;
                break;
            }
        }
        if (has_feedback_binding) {
            return;
        }

        for (const auto& site : sites) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = site};
            diag.message = std::string{
                "`WriteSamplerFeedback*` call has no `FeedbackTexture2D[Array]` "
                "binding visible in reflection -- if no streaming system consumes "
                "the feedback texture, this write is dead bandwidth on every IHV "
                "that materialises sampler feedback"};
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_sampler_feedback_without_streaming_flag() {
    return std::make_unique<SamplerFeedbackWithoutStreamingFlag>();
}

}  // namespace shader_clippy::rules
