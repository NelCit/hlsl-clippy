// rov-without-earlydepthstencil
//
// Detects pixel shaders that declare a `RasterizerOrdered*` (ROV) resource
// without the `[earlydepthstencil]` attribute and without an obvious
// `discard` / depth-write hazard. ROVs serialise per-pixel writes; pairing
// them with the early-depth-stencil attribute lets the rasterizer skip
// shading for occluded pixels entirely.
//
// Detection plan: AST-only. Walk the source for a `RasterizerOrdered`-prefixed
// identifier (covers `RasterizerOrderedTexture2D`, `RasterizerOrderedBuffer`,
// `RasterizerOrderedByteAddressBuffer`, etc.). When at least one is found,
// scan for the `[earlydepthstencil]` attribute. If absent and the source
// also contains no `discard` / `clip` / `SV_Depth*` write, emit at the file
// start.

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

constexpr std::string_view k_rule_id = "rov-without-earlydepthstencil";
constexpr std::string_view k_category = "bindings";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::pair<bool, std::uint32_t> find_rov(std::string_view bytes) noexcept {
    constexpr std::string_view k_prefix = "RasterizerOrdered";
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(k_prefix, pos);
        if (found == std::string_view::npos)
            return {false, 0U};
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        if (ok_left)
            return {true, static_cast<std::uint32_t>(found)};
        pos = found + 1U;
    }
    return {false, 0U};
}

[[nodiscard]] bool contains_keyword(std::string_view bytes, std::string_view kw) noexcept {
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(kw, pos);
        if (found == std::string_view::npos)
            return false;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (ok_left && ok_right)
            return true;
        pos = found + 1U;
    }
    return false;
}

class RovWithoutEarlyDepthStencil : public Rule {
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
        const auto [has_rov, rov_pos] = find_rov(bytes);
        if (!has_rov)
            return;
        if (bytes.find("earlydepthstencil") != std::string_view::npos)
            return;
        // Skip if the source has any of the per-pixel hazards.
        if (contains_keyword(bytes, "discard") || contains_keyword(bytes, "clip") ||
            bytes.find("SV_Depth") != std::string_view::npos ||
            bytes.find("SV_StencilRef") != std::string_view::npos)
            return;
        const std::uint32_t rov_end =
            rov_pos + static_cast<std::uint32_t>(std::string_view{"RasterizerOrdered"}.size());
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(), .bytes = ByteSpan{rov_pos, rov_end}};
        diag.message = std::string{
            "pixel shader uses a `RasterizerOrdered*` (ROV) resource without "
            "`[earlydepthstencil]` and no `discard` / `clip` / `SV_Depth*` write -- adding "
            "`[earlydepthstencil]` lets the rasterizer skip shading on occluded pixels and "
            "halves the ROV serialisation cost on every IHV"};
        ctx.emit(std::move(diag));
    }
};

}  // namespace

std::unique_ptr<Rule> make_rov_without_earlydepthstencil() {
    return std::make_unique<RovWithoutEarlyDepthStencil>();
}

}  // namespace hlsl_clippy::rules
