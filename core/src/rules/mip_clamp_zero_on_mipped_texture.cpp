// mip-clamp-zero-on-mipped-texture
//
// `MaxLOD = 0` (or `MinMipLevel = 0` clamp) on a sampler bound to a fully-
// mipped texture silently disables all mip filtering: the sampler always
// reads mip 0 regardless of derivative magnitude. This costs bandwidth on
// minified surfaces and aliases on terrain / streaming workloads.
//
// Detection (Reflection stage with AST fallback):
//   - Preferred path: the sampler descriptor (`SamplerDescriptor::max_lod`)
//     reports a value at or near 0 on a sampler whose binding kind is
//     `SamplerState`. Today the bridge does not surface descriptor state, so
//     this branch is forward-compatible.
//   - AST fallback: the source contains a literal assignment of the form
//     `<sampler>.MaxLOD = 0;` where `<sampler>` is a sampler binding visible
//     in reflection. Mirrors the inline-static-sampler-state syntax common
//     in HLSL examples.
//
// Suggestion-grade; no machine-applicable fix (the right fix is to remove the
// MaxLOD clamp or set it to `FLT_MAX`, which depends on author intent).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/reflect_resource.hpp"
#include "rules/util/reflect_sampler.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "mip-clamp-zero-on-mipped-texture";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Find the next byte offset of `needle` in `haystack` starting at `from`
/// where `needle` is preceded by a non-identifier char (or start-of-string).
[[nodiscard]] std::size_t find_token_lhs(std::string_view haystack,
                                         std::string_view needle,
                                         std::size_t from) noexcept {
    std::size_t pos = from;
    while (pos <= haystack.size()) {
        const auto found = haystack.find(needle, pos);
        if (found == std::string_view::npos) {
            return std::string_view::npos;
        }
        const bool ok_left = (found == 0) || is_id_boundary(haystack[found - 1]);
        if (ok_left) {
            return found;
        }
        pos = found + 1;
    }
    return std::string_view::npos;
}

/// True if the substring beginning at `start` in `text` matches `<sampler>.MaxLOD = 0`
/// up to whitespace tolerance. `sampler` is the sampler binding's name.
[[nodiscard]] bool matches_max_lod_zero(std::string_view text,
                                        std::size_t start,
                                        std::string_view sampler) noexcept {
    if (start + sampler.size() > text.size()) {
        return false;
    }
    if (text.substr(start, sampler.size()) != sampler) {
        return false;
    }
    std::size_t i = start + sampler.size();
    if (i >= text.size() || text[i] != '.') {
        return false;
    }
    ++i;
    constexpr std::string_view k_max_lod = "MaxLOD";
    if (i + k_max_lod.size() > text.size() || text.substr(i, k_max_lod.size()) != k_max_lod) {
        return false;
    }
    i += k_max_lod.size();
    if (i < text.size() && !is_id_boundary(text[i])) {
        return false;
    }
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    if (i >= text.size() || text[i] != '=') {
        return false;
    }
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    // Match a literal 0, 0.0, 0.0f.
    if (i >= text.size()) {
        return false;
    }
    if (text[i] != '0') {
        return false;
    }
    std::size_t j = i + 1;
    if (j < text.size() && text[j] == '.') {
        ++j;
        while (j < text.size() && text[j] == '0') {
            ++j;
        }
    }
    if (j < text.size() && (text[j] == 'f' || text[j] == 'F')) {
        ++j;
    }
    if (j < text.size() && !is_id_boundary(text[j])) {
        return false;
    }
    return true;
}

class MipClampZeroOnMippedTexture : public Rule {
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
            if (binding.kind != ResourceKind::SamplerState) {
                continue;
            }

            // Preferred path: SamplerDescriptor reports MaxLOD <= 0. Today
            // sampler_descriptor_for() returns nullopt; this branch starts
            // firing as the bridge surfaces descriptor state.
            const auto desc = util::sampler_descriptor_for(reflection, binding.name);
            bool emit = false;
            if (desc.has_value() && desc->max_lod.has_value() && *desc->max_lod <= 0.0f) {
                emit = true;
            }

            // AST fallback: literal `<sampler>.MaxLOD = 0` in source.
            ByteSpan ast_match{};
            if (!emit) {
                std::size_t pos = 0;
                while (pos < bytes.size()) {
                    const auto found = find_token_lhs(bytes, binding.name, pos);
                    if (found == std::string_view::npos) {
                        break;
                    }
                    if (matches_max_lod_zero(bytes, found, binding.name)) {
                        emit = true;
                        ast_match.lo = static_cast<std::uint32_t>(found);
                        ast_match.hi =
                            static_cast<std::uint32_t>(found + binding.name.size() + 1U + 6U);
                        break;
                    }
                    pos = found + 1;
                }
            }

            if (!emit) {
                continue;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            const ByteSpan span =
                (ast_match.hi > ast_match.lo) ? ast_match : binding.declaration_span.bytes;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = span};
            diag.message = std::string{"sampler `"} + binding.name +
                           "` clamps `MaxLOD` to 0 on a mipped texture -- this disables all "
                           "mip filtering, costs bandwidth (always samples mip 0) and aliases "
                           "on minified surfaces; remove the clamp or set it to `FLT_MAX`";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_mip_clamp_zero_on_mipped_texture() {
    return std::make_unique<MipClampZeroOnMippedTexture>();
}

}  // namespace shader_clippy::rules
