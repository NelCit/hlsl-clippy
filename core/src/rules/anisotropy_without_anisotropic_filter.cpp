// anisotropy-without-anisotropic-filter
//
// `MaxAnisotropy > 1` on a sampler whose `Filter` mode does not request
// anisotropic filtering is silently ignored on every IHV. The author's intent
// (anisotropic filtering for grazing-angle textures) regresses to the default
// trilinear / linear / point filter, and the descriptor still pays the cost
// of carrying the unused MaxAnisotropy field.
//
// Detection (Reflection stage):
//   - Preferred path: `SamplerDescriptor::max_anisotropy > 1` AND the
//     `filter` field's name does not contain `"ANISOTROPIC"`. Today the
//     bridge does not surface descriptor state so this branch is forward-
//     compatible.
//   - AST fallback: source contains `<sampler>.MaxAnisotropy = K` with K > 1
//     AND a `<sampler>.Filter = ...` assignment whose RHS does not mention
//     `ANISOTROPIC`. The sampler must be a `SamplerState` binding visible in
//     reflection.
//
// Suggestion-grade; no machine-applicable fix (the right action is a design
// decision: either switch the filter to `*ANISOTROPIC*` or drop the
// MaxAnisotropy clamp).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rules/util/reflect_resource.hpp"
#include "rules/util/reflect_sampler.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "anisotropy-without-anisotropic-filter";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

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

/// Scan for `<sampler>.MaxAnisotropy = N` where N > 1. Returns the integer
/// value found, or 0 if the assignment is absent / unparseable.
[[nodiscard]] std::uint32_t find_max_anisotropy_value(std::string_view text,
                                                      std::string_view sampler) noexcept {
    constexpr std::string_view k_field = ".MaxAnisotropy";
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = find_token_lhs(text, sampler, pos);
        if (found == std::string_view::npos) {
            return 0;
        }
        std::size_t i = found + sampler.size();
        if (i + k_field.size() > text.size() || text.substr(i, k_field.size()) != k_field) {
            pos = found + 1;
            continue;
        }
        i += k_field.size();
        if (i < text.size() && !is_id_boundary(text[i])) {
            pos = found + 1;
            continue;
        }
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        if (i >= text.size() || text[i] != '=') {
            pos = found + 1;
            continue;
        }
        ++i;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        // Read an integer literal.
        std::uint32_t value = 0;
        bool any = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = value * 10U + static_cast<std::uint32_t>(text[i] - '0');
            ++i;
            any = true;
        }
        if (any) {
            return value;
        }
        pos = found + 1;
    }
    return 0;
}

/// Read the RHS of `<sampler>.Filter = <ident>;` and return whether the RHS
/// identifier contains the substring `"ANISOTROPIC"`. Returns true if no
/// `Filter` assignment is present (we treat absent-filter as "no anisotropy
/// requested" since the default filter modes are not anisotropic).
[[nodiscard]] bool filter_is_anisotropic(std::string_view text, std::string_view sampler) noexcept {
    constexpr std::string_view k_field = ".Filter";
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto found = find_token_lhs(text, sampler, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        std::size_t i = found + sampler.size();
        if (i + k_field.size() > text.size() || text.substr(i, k_field.size()) != k_field) {
            pos = found + 1;
            continue;
        }
        i += k_field.size();
        if (i < text.size() && !is_id_boundary(text[i])) {
            pos = found + 1;
            continue;
        }
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        if (i >= text.size() || text[i] != '=') {
            pos = found + 1;
            continue;
        }
        ++i;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        const std::size_t rhs_start = i;
        while (i < text.size() && text[i] != ';' && text[i] != '\n' && text[i] != '\r') {
            ++i;
        }
        const auto rhs = text.substr(rhs_start, i - rhs_start);
        return rhs.find("ANISOTROPIC") != std::string_view::npos;
    }
    return false;
}

class AnisotropyWithoutAnisotropicFilter : public Rule {
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
            if (!util::is_sampler(binding.kind)) {
                continue;
            }

            // Preferred path: use SamplerDescriptor when available.
            const auto desc = util::sampler_descriptor_for(reflection, binding.name);
            bool emit = false;
            if (desc.has_value() && desc->max_anisotropy.has_value() &&
                *desc->max_anisotropy > 1U) {
                const bool filter_aniso = desc->filter.has_value() &&
                                          desc->filter->find("ANISOTROPIC") != std::string::npos;
                emit = !filter_aniso;
            }

            // AST fallback path.
            if (!emit) {
                const auto value = find_max_anisotropy_value(bytes, binding.name);
                if (value > 1U) {
                    if (!filter_is_anisotropic(bytes, binding.name)) {
                        emit = true;
                    }
                }
            }

            if (!emit) {
                continue;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = binding.declaration_span.bytes,
            };
            diag.message = std::string{"sampler `"} + binding.name +
                           "` sets `MaxAnisotropy > 1` but its `Filter` does not request "
                           "anisotropic filtering -- the MaxAnisotropy clamp is silently "
                           "ignored on every IHV; either set `Filter = ANISOTROPIC` or drop "
                           "the MaxAnisotropy clamp";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_anisotropy_without_anisotropic_filter() {
    return std::make_unique<AnisotropyWithoutAnisotropicFilter>();
}

}  // namespace shader_clippy::rules
