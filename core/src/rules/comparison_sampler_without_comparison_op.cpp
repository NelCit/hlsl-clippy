// comparison-sampler-without-comparison-op
//
// `SamplerComparisonState` exists to drive the hardware comparison-and-filter
// path (PCF for shadow maps). If a SamplerComparisonState binding is declared
// but only `Sample`/`SampleLevel`/`SampleBias`/`SampleGrad` (the non-`Cmp`
// variants) are called against it, the descriptor slot is wasted and readers
// expect PCF behaviour where there is none.
//
// Detection (Reflection stage):
//   1. Reflection identifies the binding kind as `SamplerComparisonState`.
//   2. AST scan: count occurrences of `<texture>.SampleCmp*` calls anywhere
//      in source whose second argument syntactically references the
//      sampler's name; if zero such calls exist AND there is at least one
//      `Sample*` (non-`Cmp`) call referencing the sampler's name, emit the
//      diagnostic anchored at the binding's `declaration_span`.
//
// The detection deliberately uses textual identifier matching rather than
// full call-site resolution because the ADR 0011 spec calls this out as a
// reflection-aware rule (need binding kind) where the call-site discrimination
// is purely syntactic. Suggestion-grade; no fix.

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

constexpr std::string_view k_rule_id = "comparison-sampler-without-comparison-op";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Count distinct occurrences of `name` as a complete identifier in `text`.
[[nodiscard]] std::size_t count_ident_occurrences(std::string_view text,
                                                  std::string_view name) noexcept {
    if (name.empty() || text.size() < name.size()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t pos = 0;
    while (pos <= text.size() - name.size()) {
        const auto found = text.find(name, pos);
        if (found == std::string_view::npos) {
            break;
        }
        const bool ok_left = (found == 0) || is_id_boundary(text[found - 1]);
        const std::size_t end = found + name.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            ++count;
        }
        pos = found + 1;
    }
    return count;
}

/// True when at least one `*.SampleCmp[*]` call in source has the sampler's
/// name as its first call argument (the second argument lexically because the
/// first is the texture).
///
/// Strategy: search for "SampleCmp" tokens in source; for each, scan back over
/// whitespace and `(`-tokens to find the call's argument list, then check
/// whether the sampler's name appears as the first comma-separated arg.
[[nodiscard]] bool has_cmp_call_with_sampler(std::string_view text,
                                             std::string_view sampler_name) noexcept {
    constexpr std::string_view k_needle = "SampleCmp";
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = text.find(k_needle, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        // Sampling pattern: `tex.SampleCmp*(sampler, ...)` -- find the next
        // `(` after `SampleCmp[Optional]`.
        std::size_t i = found + k_needle.size();
        while (i < text.size() &&
               (((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z')))) {
            ++i;
        }
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        if (i < text.size() && text[i] == '(') {
            ++i;
            while (i < text.size() &&
                   (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
                ++i;
            }
            // Read the first arg up to ',' or ')'.
            const std::size_t arg_start = i;
            while (i < text.size() && text[i] != ',' && text[i] != ')') {
                ++i;
            }
            // Trim trailing whitespace.
            std::size_t arg_end = i;
            while (arg_end > arg_start &&
                   (text[arg_end - 1] == ' ' || text[arg_end - 1] == '\t' ||
                    text[arg_end - 1] == '\n' || text[arg_end - 1] == '\r')) {
                --arg_end;
            }
            const auto first_arg = text.substr(arg_start, arg_end - arg_start);
            // Match sampler name as a token within the first arg.
            if (count_ident_occurrences(first_arg, sampler_name) > 0) {
                return true;
            }
        }
        pos = found + k_needle.size();
    }
    return false;
}

/// Same as `has_cmp_call_with_sampler` but for the non-Cmp `Sample*` family.
[[nodiscard]] bool has_non_cmp_call_with_sampler(std::string_view text,
                                                 std::string_view sampler_name) noexcept {
    constexpr std::string_view k_needle = ".Sample";
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = text.find(k_needle, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        std::size_t i = found + k_needle.size();
        // Skip remainder of method name (letters only).
        const std::size_t name_start = i;
        while (i < text.size() &&
               ((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z'))) {
            ++i;
        }
        const auto suffix = text.substr(name_start, i - name_start);
        // Reject Cmp variants.
        const bool is_cmp = suffix.find("Cmp") != std::string_view::npos;
        if (is_cmp) {
            pos = found + k_needle.size();
            continue;
        }
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        if (i < text.size() && text[i] == '(') {
            ++i;
            while (i < text.size() &&
                   (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
                ++i;
            }
            const std::size_t arg_start = i;
            while (i < text.size() && text[i] != ',' && text[i] != ')') {
                ++i;
            }
            std::size_t arg_end = i;
            while (arg_end > arg_start &&
                   (text[arg_end - 1] == ' ' || text[arg_end - 1] == '\t' ||
                    text[arg_end - 1] == '\n' || text[arg_end - 1] == '\r')) {
                --arg_end;
            }
            const auto first_arg = text.substr(arg_start, arg_end - arg_start);
            if (count_ident_occurrences(first_arg, sampler_name) > 0) {
                return true;
            }
        }
        pos = found + k_needle.size();
    }
    return false;
}

class ComparisonSamplerWithoutComparisonOp : public Rule {
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
        for (const auto& binding : reflection.bindings) {
            if (binding.kind != ResourceKind::SamplerComparisonState) {
                continue;
            }
            const bool has_cmp = has_cmp_call_with_sampler(bytes, binding.name);
            if (has_cmp) {
                continue;
            }
            const bool has_non_cmp = has_non_cmp_call_with_sampler(bytes, binding.name);
            if (!has_non_cmp) {
                // Sampler unused or only used in patterns we don't recognise.
                continue;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = binding.declaration_span.bytes,
            };
            diag.message = std::string{"`SamplerComparisonState "} + binding.name +
                           "` is declared but only used with non-`Cmp` Sample variants -- "
                           "either switch the call sites to `SampleCmp*` (PCF) or change "
                           "the binding to a plain `SamplerState`";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_comparison_sampler_without_comparison_op() {
    return std::make_unique<ComparisonSamplerWithoutComparisonOp>();
}

}  // namespace shader_clippy::rules
