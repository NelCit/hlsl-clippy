// vrs-incompatible-output
//
// Detects pixel shaders that write `SV_Depth` / `SV_StencilRef` or call
// `discard` / `clip` while the project declares per-draw or per-primitive
// shading rate. These outputs silently force fine-rate (1x1) shading on
// every IHV, defeating the VRS savings.
//
// Detection plan: AST. The shader has no portable way to know whether the
// pipeline declares VRS (that lives in the C++ side). We approximate by
// emitting a `Note` whenever a pixel-shader-style file writes one of the
// hazard outputs. The note carries enough text for the reader to decide
// whether VRS is on this pipeline.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "vrs-incompatible-output";
constexpr std::string_view k_category = "vrs";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::pair<bool, std::uint32_t> find_keyword(std::string_view bytes,
                                                          std::string_view kw) noexcept {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(kw, pos);
        if (found == std::string_view::npos)
            return {false, 0U};
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (ok_left && ok_right)
            return {true, static_cast<std::uint32_t>(found)};
        pos = found + 1U;
    }
    return {false, 0U};
}

class VrsIncompatibleOutput : public Rule {
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
        const bool is_pixel_file = bytes.find("\"pixel\"") != std::string_view::npos ||
                                   bytes.find("SV_Target") != std::string_view::npos;
        if (!is_pixel_file)
            return;
        for (const std::string_view kw : {"SV_Depth",
                                          "SV_StencilRef",
                                          "SV_DepthGreaterEqual",
                                          "SV_DepthLessEqual",
                                          "discard",
                                          "clip"}) {
            const auto [hit, off] = find_keyword(bytes, kw);
            if (!hit)
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span =
                Span{.source = tree.source_id(),
                     .bytes = ByteSpan{off, static_cast<std::uint32_t>(off + kw.size())}};
            diag.message =
                std::string{"pixel shader uses `"} + std::string{kw} +
                "` -- if this PSO has per-draw / per-primitive shading-rate enabled the GPU "
                "silently forces fine-rate (1x1) shading and the VRS savings are lost";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_vrs_incompatible_output() {
    return std::make_unique<VrsIncompatibleOutput>();
}

}  // namespace hlsl_clippy::rules
