// numwaves-anchored-cap
//
// Defensive rule for HLSL specs proposal 0054 (`numWaves`, under-
// consideration). Detects compute kernels with `[numthreads(X, Y, Z)]`
// where `X*Y*Z` exceeds the current per-group lane cap of 1024. When
// proposal 0054 ships, the detection extends to the new attribute; for
// now the rule flags exceedance of the existing cap.
//
// Stage: Ast. Suggestion-grade -- the cap may change with proposal 0054.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "numwaves-anchored-cap";
constexpr std::string_view k_category = "workgroup";
constexpr std::uint32_t k_lane_cap = 1024U;

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

class NumWavesAnchoredCap : public Rule {
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
        constexpr std::string_view k_needle = "numthreads(";
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(k_needle, pos);
            if (found == std::string_view::npos) {
                return;
            }
            std::size_t k = found;
            while (k > 0U && (bytes[k - 1U] == ' ' || bytes[k - 1U] == '\t')) {
                --k;
            }
            if (k == 0U || bytes[k - 1U] != '[') {
                pos = found + 1U;
                continue;
            }
            const std::size_t lp = found + std::string_view{"numthreads"}.size();
            int depth = 0;
            std::size_t i = lp;
            while (i < bytes.size()) {
                if (bytes[i] == '(') {
                    ++depth;
                } else if (bytes[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                ++i;
            }
            if (i >= bytes.size()) {
                pos = found + 1U;
                continue;
            }
            const auto inside = bytes.substr(lp + 1U, i - lp - 1U);
            std::array<std::uint32_t, 3U> dims{};
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
            const std::uint64_t total = static_cast<std::uint64_t>(dims[0]) *
                                        static_cast<std::uint64_t>(dims[1]) *
                                        static_cast<std::uint64_t>(dims[2]);
            if (total > k_lane_cap) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{
                    .source = tree.source_id(),
                    .bytes = ByteSpan{static_cast<std::uint32_t>(found - 1U),
                                      static_cast<std::uint32_t>(i + 1U)},
                };
                diag.message = std::string{"(suggestion) `[numthreads("} + std::to_string(dims[0]) +
                               ", " + std::to_string(dims[1]) + ", " + std::to_string(dims[2]) +
                               ")]` total " + std::to_string(total) +
                               " exceeds the current per-group lane "
                               "cap of 1024 -- HLSL specs proposal "
                               "0054 (`numWaves`) is under "
                               "consideration; revisit this dispatch "
                               "shape if 0054 lands";
                ctx.emit(std::move(diag));
            }
            pos = i + 1U;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_numwaves_anchored_cap() {
    return std::make_unique<NumWavesAnchoredCap>();
}

}  // namespace shader_clippy::rules
