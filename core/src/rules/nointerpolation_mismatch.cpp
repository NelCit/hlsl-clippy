// nointerpolation-mismatch
//
// Detects pixel-shader inputs treated as flat-shaded (e.g. used as integer
// indices into a structured buffer) where the corresponding vertex-shader
// output is NOT marked `nointerpolation`. Implicit linear interpolation of
// what's meant to be a flat int / index produces fractional values whose
// truncation to `uint` is order-dependent across the rasterized triangle.
//
// Detection plan: AST. Walk the source for any field declaration with a
// type tokenising to `uint*` / `int*` and a `TEXCOORDn` / `COLORn` semantic.
// When the field is NOT preceded by `nointerpolation`, emit. Conservative.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "nointerpolation-mismatch";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool type_is_integer(std::string_view t) noexcept {
    if (t.starts_with("uint") || t.starts_with("int"))
        return true;
    if (t.starts_with("min16uint") || t.starts_with("min16int"))
        return true;
    return false;
}

[[nodiscard]] bool is_interpolator_semantic(std::string_view sem) noexcept {
    sem = trim(sem);
    return sem.starts_with("TEXCOORD") || sem.starts_with("COLOR") ||
           sem.starts_with("BLENDINDICES");
}

class NointerpolationMismatch : public Rule {
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
        // Walk every `<type> <name> : <semantic>;` line at file scope or
        // inside a struct body.
        std::size_t i = 0U;
        while (i < bytes.size()) {
            // Find the next `:` semantic.
            const auto colon = bytes.find(':', i);
            if (colon == std::string_view::npos)
                return;
            // Find the end of statement.
            const auto semi = bytes.find(';', colon);
            const auto rb_or_semi = std::min(semi, bytes.find('}', colon));
            if (rb_or_semi == std::string_view::npos) {
                i = colon + 1U;
                continue;
            }
            // Read the semantic.
            const auto sem_text = trim(bytes.substr(colon + 1U, rb_or_semi - colon - 1U));
            if (!is_interpolator_semantic(sem_text)) {
                i = rb_or_semi + 1U;
                continue;
            }
            // Walk back to find the start of the field declaration.
            std::size_t start = colon;
            while (start > 0U && bytes[start - 1U] != ';' && bytes[start - 1U] != '{' &&
                   bytes[start - 1U] != ',' && bytes[start - 1U] != '\n')
                --start;
            const auto field = trim(bytes.substr(start, colon - start));
            const auto sp = field.find(' ');
            if (sp == std::string_view::npos) {
                i = rb_or_semi + 1U;
                continue;
            }
            // Determine if the field is preceded by `nointerpolation`.
            const bool has_nointerp = field.find("nointerpolation") != std::string_view::npos;
            // Determine the actual type token: skip qualifier words.
            std::string_view rest = field;
            while (!rest.empty()) {
                const auto sp2 = rest.find(' ');
                if (sp2 == std::string_view::npos)
                    break;
                const auto first = rest.substr(0, sp2);
                if (first == "nointerpolation" || first == "linear" || first == "noperspective" ||
                    first == "centroid" || first == "sample" || first == "in" || first == "out" ||
                    first == "inout" || first == "uniform") {
                    rest = trim(rest.substr(sp2));
                    continue;
                }
                break;
            }
            const auto type_sp = rest.find(' ');
            if (type_sp == std::string_view::npos) {
                i = rb_or_semi + 1U;
                continue;
            }
            const auto type_part = trim(rest.substr(0, type_sp));
            if (!type_is_integer(type_part) || has_nointerp) {
                i = rb_or_semi + 1U;
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(start),
                                                       static_cast<std::uint32_t>(rb_or_semi)}};
            diag.message = std::string{"integer interpolant `"} + std::string{type_part} +
                           " ... : " + std::string{sem_text} +
                           "` is not marked `nointerpolation` -- the rasterizer interpolates "
                           "linearly across the primitive and the truncated result is order-"
                           "dependent across lanes";
            ctx.emit(std::move(diag));
            i = rb_or_semi + 1U;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_nointerpolation_mismatch() {
    return std::make_unique<NointerpolationMismatch>();
}

}  // namespace shader_clippy::rules
