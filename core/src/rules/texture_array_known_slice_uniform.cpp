// texture-array-known-slice-uniform
//
// Detects `<texarray>.Sample(<sampler>, float3(<uv>, <K>))` where the third
// component of the UV is a compile-time constant integer. A `Texture2DArray`
// indexed by a constant slice is functionally equivalent to a `Texture2D`
// bound to that slice, with simpler descriptor handling and no array-index
// validation overhead.
//
// Detection plan: AST. Walk source bytes for `.Sample(`. After the sampler
// argument, the next argument must look like `float3(<uv>, <literal>)` or
// `float4(<uv>, <literal>, ...)`. When the slice token is a numeric literal
// or a known constant identifier (uppercase-only ALL_CAPS), emit. Suggestion
// only -- the rewrite involves changing the resource type which crosses the
// HLSL-CPU API boundary.

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

constexpr std::string_view k_rule_id = "texture-array-known-slice-uniform";
constexpr std::string_view k_category = "texture";

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool is_integer_literal(std::string_view s) noexcept {
    s = trim(s);
    if (s.empty())
        return false;
    if (s[0] == '+' || s[0] == '-')
        s.remove_prefix(1);
    if (s.empty())
        return false;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return false;
    }
    return true;
}

class TextureArrayKnownSliceUniform : public Rule {
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
        constexpr std::string_view k_needle = ".Sample(";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos)
                return;
            const std::size_t lp = found + std::string_view{".Sample"}.size();
            int depth = 0;
            std::size_t i = lp;
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
            const auto args = bytes.substr(lp + 1U, i - lp - 1U);
            // Find the second argument (after a top-level comma).
            int local_depth = 0;
            std::size_t comma = std::string_view::npos;
            for (std::size_t k = 0; k < args.size(); ++k) {
                const char c = args[k];
                if (c == '(')
                    ++local_depth;
                else if (c == ')')
                    --local_depth;
                else if (c == ',' && local_depth == 0) {
                    comma = k;
                    break;
                }
            }
            if (comma == std::string_view::npos) {
                pos = i + 1U;
                continue;
            }
            const auto uv_arg = trim(args.substr(comma + 1U));
            // Look for `float3(...)` or `float4(...)`.
            if (!(uv_arg.starts_with("float3(") || uv_arg.starts_with("float4("))) {
                pos = i + 1U;
                continue;
            }
            const auto inner_lp = uv_arg.find('(');
            const auto inner_rp = uv_arg.rfind(')');
            if (inner_lp == std::string_view::npos || inner_rp == std::string_view::npos ||
                inner_rp <= inner_lp + 1) {
                pos = i + 1U;
                continue;
            }
            const auto inner = uv_arg.substr(inner_lp + 1U, inner_rp - inner_lp - 1U);
            // Find the comma separating the uv from the slice. We want the
            // last component (third for float3, fourth for float4) to be a
            // literal.
            int d = 0;
            std::size_t last_comma = std::string_view::npos;
            for (std::size_t k = 0; k < inner.size(); ++k) {
                const char c = inner[k];
                if (c == '(')
                    ++d;
                else if (c == ')')
                    --d;
                else if (c == ',' && d == 0)
                    last_comma = k;
            }
            if (last_comma == std::string_view::npos) {
                pos = i + 1U;
                continue;
            }
            const auto slice = trim(inner.substr(last_comma + 1U));
            if (!is_integer_literal(slice)) {
                pos = i + 1U;
                continue;
            }
            const auto call_lo = found + 1U;
            const auto call_hi = i + 1U;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(call_lo),
                                                       static_cast<std::uint32_t>(call_hi)}};
            diag.message =
                std::string{"Texture2DArray-style `Sample` with a constant slice index `"} +
                std::string{slice} +
                "` is equivalent to a Texture2D bound to that slice -- consider demoting the "
                "binding type to drop the array-index validation overhead";
            ctx.emit(std::move(diag));
            pos = call_hi;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_texture_array_known_slice_uniform() {
    return std::make_unique<TextureArrayKnownSliceUniform>();
}

}  // namespace hlsl_clippy::rules
