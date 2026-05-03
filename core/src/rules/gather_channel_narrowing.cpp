// gather-channel-narrowing
//
// Detects `<tex>.Gather(<sampler>, <uv>).<channel>` where only one channel
// of the gathered `float4` is consumed and the remaining three are dead.
// `GatherRed` / `GatherGreen` / `GatherBlue` / `GatherAlpha` are direct TMU
// instructions that pick the channel at instruction level and express
// intent unambiguously.
//
// Detection plan: AST. Walk source bytes for the literal call shape
// `.Gather(...)` followed by `.<channel>` where `<channel>` is exactly one
// of `r` / `g` / `b` / `a` / `x` / `y` / `z` / `w`. We do not attempt to
// verify the other channels are dead -- a single-character swizzle on a
// `Gather` result is the canonical narrowing pattern by itself.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "gather-channel-narrowing";
constexpr std::string_view k_category = "texture";

[[nodiscard]] std::string_view channel_to_method(char c) noexcept {
    switch (c) {
        case 'r':
        case 'x':
            return "GatherRed";
        case 'g':
        case 'y':
            return "GatherGreen";
        case 'b':
        case 'z':
            return "GatherBlue";
        case 'a':
        case 'w':
            return "GatherAlpha";
        default:
            return {};
    }
}

class GatherChannelNarrowing : public Rule {
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
        constexpr std::string_view k_needle = ".Gather(";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos)
                return;
            // Confirm `.Gather` is bounded as an identifier (no preceding id char).
            const std::size_t method_start = found + 1U;
            if (method_start > 0U && method_start - 1U < bytes.size() &&
                is_id_char(bytes[method_start - 1U]) && method_start - 1U > 0U &&
                bytes[method_start - 2U] != '.') {
                pos = found + 1U;
                continue;
            }
            // Confirm what follows the `(` is NOT another id-char (so we don't
            // match `.GatherRed(`).
            const std::size_t after_method = found + std::string_view{".Gather"}.size();
            if (after_method < bytes.size() && is_id_char(bytes[after_method])) {
                pos = found + 1U;
                continue;
            }
            // Find the matching `)`.
            int depth = 0;
            std::size_t i = found + std::string_view{".Gather"}.size();
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
                pos = found + 1U;
                continue;
            }
            // After the `)`, look for `.<channel>` and confirm the next char
            // is NOT an id char (so we only match a single-character swizzle).
            std::size_t k = i + 1U;
            if (k >= bytes.size() || bytes[k] != '.') {
                pos = i + 1U;
                continue;
            }
            ++k;
            if (k >= bytes.size()) {
                pos = i + 1U;
                continue;
            }
            const char ch = bytes[k];
            const auto method = channel_to_method(ch);
            if (method.empty()) {
                pos = i + 1U;
                continue;
            }
            // Confirm channel is exactly one character.
            if (k + 1U < bytes.size() && is_id_char(bytes[k + 1U])) {
                pos = i + 1U;
                continue;
            }
            const auto call_lo = found + 1U;
            const auto swizzle_hi = k + 1U;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(call_lo),
                                                       static_cast<std::uint32_t>(swizzle_hi)}};
            diag.message = std::string{"`Gather(...)."} + std::string{1U, ch} +
                           "` consumes only one channel -- replace with `" + std::string{method} +
                           "` to encode the channel at TMU instruction level and drop the swizzle";

            // Build the rewrite. Span covers `Gather(<args>).<channel>`; the
            // replacement keeps the argument list verbatim, swaps the method
            // name, and rewrites the swizzle.
            //
            // Semantics caveat:
            //   * `Gather(...).r/.x`  -> first-texel red. `GatherRed(...).r` is
            //     bit-identical: machine-applicable.
            //   * `Gather(...).g/.y`  -> second-texel red (the y-component of
            //     the gather4-red return). The intent the rule documents is
            //     "the developer wanted the green channel", so the rewrite
            //     becomes `GatherGreen(...).r` -- first-texel green. That is a
            //     semantic shift on `Gather`'s return, so the fix is
            //     suggestion-grade for non-red channels.
            //   * Same applies to `.b/.z` and `.a/.w`.
            const std::string_view args =
                bytes.substr(found + std::string_view{".Gather"}.size(),
                             i - (found + std::string_view{".Gather"}.size()) + 1U);
            std::string replacement;
            replacement.reserve(method.size() + args.size() + 3U);
            replacement.append(method);
            replacement.append(args);
            replacement.append(".r");

            const bool is_red_channel = (ch == 'r' || ch == 'x');
            Fix fix;
            fix.machine_applicable = is_red_channel;
            fix.description = is_red_channel
                                  ? std::string{"replace `Gather(...).r` with `GatherRed(...).r`"}
                                  : std::string{
                                        "replace `Gather(...).<channel>` with the channel-"
                                        "specific gather; verify the original swizzle was "
                                        "intended to mean the channel name (not a texel index)"};
            TextEdit edit;
            edit.span = Span{.source = tree.source_id(),
                             .bytes = ByteSpan{static_cast<std::uint32_t>(call_lo),
                                               static_cast<std::uint32_t>(swizzle_hi)}};
            edit.replacement = std::move(replacement);
            fix.edits.push_back(std::move(edit));
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
            pos = swizzle_hi;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_gather_channel_narrowing() {
    return std::make_unique<GatherChannelNarrowing>();
}

}  // namespace shader_clippy::rules
