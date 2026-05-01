// texture-as-buffer
//
// Detects a `Texture2D` (or 1D / 3D) binding that is only ever sampled with
// a UV whose y-component (or higher) is a compile-time zero literal -- in
// other words, a 2D texture used as a 1D linear array. The hardware path is
// to declare a `Buffer<T>` or `StructuredBuffer<T>` and skip the texture
// filter / sampler chain entirely.
//
// Detection plan: AST. For each user-declared `Texture2D[name];` binding,
// find every `<name>.Sample[Level]?(<sampler>, <uv-arg>)` call. If every
// found call has `<uv-arg>` as a `float2(<x>, 0)` or `float2(<x>, 0.0)`
// shape, emit. Conservative: a single non-zero y-call drops the rule.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "texture-as-buffer";
constexpr std::string_view k_category = "texture";

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool is_zero_literal(std::string_view s) noexcept {
    s = trim(s);
    if (s.empty())
        return false;
    if (s[0] == '+' || s[0] == '-')
        s.remove_prefix(1);
    bool seen = false;
    std::size_t i = 0U;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if (s[i] != '0')
            return false;
        seen = true;
        ++i;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (s[i] != '0')
                return false;
            seen = true;
            ++i;
        }
    }
    while (i < s.size()) {
        const char c = s[i];
        if (c != 'f' && c != 'F' && c != 'h' && c != 'H')
            return false;
        ++i;
    }
    return seen;
}

[[nodiscard]] std::vector<std::pair<std::string, std::pair<std::uint32_t, std::uint32_t>>>
collect_texture2d_decls(std::string_view bytes) {
    std::vector<std::pair<std::string, std::pair<std::uint32_t, std::uint32_t>>> out;
    constexpr std::string_view k_needle = "Texture2D";
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(k_needle, pos);
        if (found == std::string_view::npos)
            return out;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        // Reject `Texture2DArray`, `Texture2DMS*`.
        const std::size_t after = found + k_needle.size();
        const bool ok_right =
            (after >= bytes.size()) || (!is_id_char(bytes[after]) || bytes[after] == '<');
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        // Skip optional `<...>`.
        std::size_t i = after;
        if (i < bytes.size() && bytes[i] == '<') {
            int depth = 0;
            while (i < bytes.size()) {
                if (bytes[i] == '<')
                    ++depth;
                else if (bytes[i] == '>') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
                ++i;
            }
        }
        while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\t'))
            ++i;
        const std::size_t name_lo = i;
        while (i < bytes.size() && is_id_char(bytes[i]))
            ++i;
        const auto name = bytes.substr(name_lo, i - name_lo);
        if (!name.empty()) {
            out.emplace_back(std::string{name},
                             std::pair<std::uint32_t, std::uint32_t>{
                                 static_cast<std::uint32_t>(found), static_cast<std::uint32_t>(i)});
        }
        pos = i;
    }
    return out;
}

class TextureAsBuffer : public Rule {
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
        const auto decls = collect_texture2d_decls(bytes);
        for (const auto& [name, decl_span] : decls) {
            // Look for every `<name>.Sample(...)` call and check the uv arg.
            std::size_t pos = 0U;
            std::size_t call_count = 0U;
            std::size_t zero_y_count = 0U;
            std::uint32_t first_call_lo = 0U;
            std::uint32_t last_call_hi = 0U;
            while (pos < bytes.size()) {
                const auto found = bytes.find(name, pos);
                if (found == std::string_view::npos)
                    break;
                const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
                const std::size_t end = found + name.size();
                const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
                if (!ok_left || !ok_right) {
                    pos = found + 1U;
                    continue;
                }
                if (found >= decl_span.first && found < decl_span.second) {
                    pos = end;
                    continue;
                }
                if (end >= bytes.size() || bytes[end] != '.') {
                    pos = end;
                    continue;
                }
                std::size_t k = end + 1U;
                const std::size_t method_lo = k;
                while (k < bytes.size() && is_id_char(bytes[k]))
                    ++k;
                const auto method = bytes.substr(method_lo, k - method_lo);
                if (method != "Sample" && method != "SampleLevel" && method != "SampleBias") {
                    pos = end;
                    continue;
                }
                if (k >= bytes.size() || bytes[k] != '(') {
                    pos = end;
                    continue;
                }
                int depth = 0;
                std::size_t lp = k;
                std::size_t rp = lp;
                while (rp < bytes.size()) {
                    if (bytes[rp] == '(')
                        ++depth;
                    else if (bytes[rp] == ')') {
                        --depth;
                        if (depth == 0)
                            break;
                    }
                    ++rp;
                }
                if (rp >= bytes.size()) {
                    pos = end;
                    continue;
                }
                const auto args = bytes.substr(lp + 1U, rp - lp - 1U);
                // Find second arg (uv).
                int d2 = 0;
                std::size_t comma = std::string_view::npos;
                for (std::size_t j = 0; j < args.size(); ++j) {
                    const char c = args[j];
                    if (c == '(')
                        ++d2;
                    else if (c == ')')
                        --d2;
                    else if (c == ',' && d2 == 0) {
                        comma = j;
                        break;
                    }
                }
                if (comma == std::string_view::npos) {
                    pos = rp + 1U;
                    continue;
                }
                ++call_count;
                if (call_count == 1U)
                    first_call_lo = static_cast<std::uint32_t>(found);
                last_call_hi = static_cast<std::uint32_t>(rp + 1U);
                const auto uv = trim(args.substr(comma + 1U));
                if (uv.starts_with("float2(")) {
                    const auto lp2 = uv.find('(');
                    const auto rp2 = uv.rfind(')');
                    if (lp2 != std::string_view::npos && rp2 != std::string_view::npos &&
                        rp2 > lp2 + 1) {
                        const auto inner = uv.substr(lp2 + 1U, rp2 - lp2 - 1U);
                        // Find the comma at top level.
                        int d3 = 0;
                        std::size_t c2 = std::string_view::npos;
                        for (std::size_t j = 0; j < inner.size(); ++j) {
                            const char c = inner[j];
                            if (c == '(')
                                ++d3;
                            else if (c == ')')
                                --d3;
                            else if (c == ',' && d3 == 0) {
                                c2 = j;
                                break;
                            }
                        }
                        if (c2 != std::string_view::npos &&
                            is_zero_literal(inner.substr(c2 + 1U))) {
                            ++zero_y_count;
                        }
                    }
                }
                pos = rp + 1U;
            }
            if (call_count >= 1U && call_count == zero_y_count) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span = Span{.source = tree.source_id(),
                                         .bytes = ByteSpan{first_call_lo, last_call_hi}};
                diag.message = std::string{"`"} + name +
                               "` is a Texture2D but every `Sample*` call passes a uv with y == 0 "
                               "-- consider rebinding as `Buffer<T>` / `StructuredBuffer<T>` to "
                               "skip the texture-filter chain";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_texture_as_buffer() {
    return std::make_unique<TextureAsBuffer>();
}

}  // namespace hlsl_clippy::rules
