// numthreads-not-wave-aligned
//
// Detects compute / mesh / amplification entry points whose `[numthreads(X,
// Y, Z)]` total is not a multiple of the configured target wave size
// (default 32, matching NVIDIA Turing/Ada and AMD RDNA wave32). The final
// wave of every group launches with masked-off lanes that consume a wave
// slot for less than full work.
//
// Detection plan: AST. Walk the source for `[numthreads(...)]` attributes,
// constant-fold the three integer arguments, and emit when the product is
// not a multiple of 32 (and is at least 32; the smaller-than-wave case is
// `numthreads-too-small`).

#include <array>
#include <cctype>
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

constexpr std::string_view k_rule_id = "numthreads-not-wave-aligned";
constexpr std::string_view k_category = "workgroup";
constexpr std::uint32_t k_target_wave_size = 32U;

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

[[nodiscard]] bool parse_integer(std::string_view s, std::uint32_t& out) noexcept {
    s = trim(s);
    if (s.empty())
        return false;
    std::uint32_t v = 0U;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return false;
        v = v * 10U + static_cast<std::uint32_t>(c - '0');
    }
    out = v;
    return true;
}

void scan_numthreads(const AstTree& tree, std::string_view bytes, RuleContext& ctx) {
    constexpr std::string_view k_needle = "numthreads(";
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(k_needle, pos);
        if (found == std::string_view::npos)
            return;
        // Confirm this is preceded by `[`.
        std::size_t k = found;
        while (k > 0U && (bytes[k - 1U] == ' ' || bytes[k - 1U] == '\t'))
            --k;
        if (k == 0U || bytes[k - 1U] != '[') {
            pos = found + 1U;
            continue;
        }
        // Find the matching `)`.
        const std::size_t lp = found + std::string_view{"numthreads"}.size();
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
        const auto inside = bytes.substr(lp + 1U, i - lp - 1U);
        // Split by commas at top level.
        std::array<std::uint32_t, 3U> dims{0U, 0U, 0U};
        std::size_t dim_idx = 0U;
        std::size_t arg_start = 0U;
        for (std::size_t j = 0U; j <= inside.size(); ++j) {
            if (j == inside.size() || inside[j] == ',') {
                if (dim_idx >= dims.size()) {
                    dim_idx = 99U;
                    break;
                }
                std::uint32_t v = 0U;
                if (!parse_integer(inside.substr(arg_start, j - arg_start), v)) {
                    dim_idx = 99U;
                    break;
                }
                dims[dim_idx++] = v;
                arg_start = j + 1U;
            }
        }
        if (dim_idx != 3U) {
            pos = i + 1U;
            continue;
        }
        const std::uint32_t total = dims[0] * dims[1] * dims[2];
        if (total < k_target_wave_size || (total % k_target_wave_size) == 0U) {
            pos = i + 1U;
            continue;
        }
        Diagnostic diag;
        diag.code = std::string{k_rule_id};
        diag.severity = Severity::Warning;
        diag.primary_span = Span{.source = tree.source_id(),
                                 .bytes = ByteSpan{static_cast<std::uint32_t>(found - 1U),
                                                   static_cast<std::uint32_t>(i + 1U)}};
        diag.message = std::string{"`[numthreads("} + std::to_string(dims[0]) + ", " +
                       std::to_string(dims[1]) + ", " + std::to_string(dims[2]) + ")]` total " +
                       std::to_string(total) + " is not a multiple of " +
                       std::to_string(k_target_wave_size) +
                       " -- the final wave of every group launches with masked-off lanes that "
                       "still consume a full wave slot";
        ctx.emit(std::move(diag));
        pos = i + 1U;
    }
}

class NumthreadsNotWaveAligned : public Rule {
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
        scan_numthreads(tree, tree.source_bytes(), ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_numthreads_not_wave_aligned() {
    return std::make_unique<NumthreadsNotWaveAligned>();
}

}  // namespace hlsl_clippy::rules
