// sv-depth-vs-conservative-depth
//
// Detects pixel shaders that write `SV_Depth` (a free-form depth output)
// where the value is monotonically `>=` rasterized depth or `<=` rasterized
// depth. The HLSL spec exposes `SV_DepthGreaterEqual` and `SV_DepthLessEqual`
// as conservative-depth outputs that preserve early-Z; using `SV_Depth`
// disables early-Z on every IHV.
//
// Detection plan: AST. We have no easy way to prove monotonicity at this
// stage; emit a `Note` whenever a pixel shader writes `SV_Depth` to remind
// the author of the conservative-depth alternatives.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "sv-depth-vs-conservative-depth";
constexpr std::string_view k_category = "vrs";

class SvDepthVsConservativeDepth : public Rule {
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
        const bool is_pixel = bytes.find("\"pixel\"") != std::string_view::npos ||
                              bytes.find("SV_Target") != std::string_view::npos;
        if (!is_pixel)
            return;
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find("SV_Depth", pos);
            if (found == std::string_view::npos)
                return;
            const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
            const std::size_t end = found + std::string_view{"SV_Depth"}.size();
            const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
            // Reject the matching variants we want to keep -- check the chars
            // directly after `SV_Depth` for `Greater` / `Less`.
            if (!ok_left || !ok_right) {
                pos = found + 1U;
                continue;
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                       static_cast<std::uint32_t>(end)}};
            diag.message = std::string{
                "`SV_Depth` disables early-Z on every IHV -- if the written value is "
                "monotonically >= or <= the rasterized depth, switch to "
                "`SV_DepthGreaterEqual` / `SV_DepthLessEqual` to keep early-Z"};
            ctx.emit(std::move(diag));
            pos = end;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_sv_depth_vs_conservative_depth() {
    return std::make_unique<SvDepthVsConservativeDepth>();
}

}  // namespace shader_clippy::rules
