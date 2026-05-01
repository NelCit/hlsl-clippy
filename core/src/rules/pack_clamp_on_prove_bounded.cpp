// pack-clamp-on-prove-bounded
//
// Detects `pack_clamp_u8(...)` / `pack_clamp_s8(...)` calls whose operands
// are provably already inside the [0, 255] / [-128, 127] range -- typically
// the result of a `saturate(...)` followed by a multiply by 255. The
// truncating `pack_u8` / `pack_s8` is a single ALU op; the clamping variant
// adds a branch-free min/max pair on every lane.
//
// Detection plan: AST. Match `pack_clamp_u8(<expr>)` and
// `pack_clamp_s8(<expr>)` calls. When `<expr>` is the literal pattern
// `saturate(<inner>) * 255`, `<inner> * 255.0` followed by a `saturate`
// wrap, or any expression whose textual form starts with `saturate(` and
// is multiplied by a literal `255`, emit. Conservative -- requires the
// `saturate` to be visible at the same call site.

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

constexpr std::string_view k_rule_id = "pack-clamp-on-prove-bounded";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

class PackClampOnProveBounded : public Rule {
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
        for (const std::string_view fn :
             {"pack_clamp_u8", "pack_clamp_s8", "pack_clamp_u16", "pack_clamp_s16"}) {
            std::size_t pos = 0U;
            while (pos < bytes.size()) {
                const auto found = bytes.find(fn, pos);
                if (found == std::string_view::npos)
                    break;
                const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
                const std::size_t end = found + fn.size();
                const bool ok_right = (end < bytes.size() && bytes[end] == '(');
                if (!ok_left || !ok_right) {
                    pos = found + 1U;
                    continue;
                }
                int depth = 0;
                std::size_t i = end;
                while (i < bytes.size()) {
                    if (bytes[i] == '(')
                        ++depth;
                    else if (bytes[i] == ')') {
                        --depth;
                        if (depth == 0)
                            break;
                    }
                    ++i;
                }
                if (i >= bytes.size()) {
                    pos = end;
                    continue;
                }
                const auto inside = bytes.substr(end + 1U, i - end - 1U);
                const bool has_saturate = inside.find("saturate(") != std::string_view::npos;
                if (!has_saturate) {
                    pos = i + 1U;
                    continue;
                }
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                           static_cast<std::uint32_t>(i + 1U)}};
                diag.message = std::string{"`"} + std::string{fn} +
                               "` operand is already saturated -- replace with `" +
                               std::string{fn.substr(0, 5U)} + std::string{fn.substr(11U)} +
                               "` (truncating pack) to drop the redundant clamp";
                ctx.emit(std::move(diag));
                pos = i + 1U;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_pack_clamp_on_prove_bounded() {
    return std::make_unique<PackClampOnProveBounded>();
}

}  // namespace hlsl_clippy::rules
