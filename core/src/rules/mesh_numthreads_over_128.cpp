// mesh-numthreads-over-128
//
// Detects mesh / amplification entry points whose `[numthreads(X, Y, Z)]`
// total exceeds 128. The D3D12 mesh-pipeline spec caps both stages at 128
// threads per group; values above the cap fail PSO creation.
//
// Detection plan: AST. Walk for `[shader("mesh")]` / `[shader("amplification")]`
// attributes; for each, find the nearest following `[numthreads(...)]`,
// constant-fold the three integer arguments, and emit when the product
// exceeds 128.

#include <array>
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

constexpr std::string_view k_rule_id = "mesh-numthreads-over-128";
constexpr std::string_view k_category = "mesh";
constexpr std::uint32_t k_cap = 128U;

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

[[nodiscard]] bool parse_numthreads_after(std::string_view bytes,
                                          std::size_t start,
                                          std::array<std::uint32_t, 3>& dims_out,
                                          std::size_t& attr_lo_out,
                                          std::size_t& attr_hi_out) noexcept {
    const auto found = bytes.find("numthreads(", start);
    if (found == std::string_view::npos)
        return false;
    // Confirm preceded by `[`.
    std::size_t k = found;
    while (k > 0U && (bytes[k - 1U] == ' ' || bytes[k - 1U] == '\t' || bytes[k - 1U] == '\n'))
        --k;
    if (k == 0U || bytes[k - 1U] != '[')
        return false;
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
    if (i >= bytes.size())
        return false;
    const auto inside = bytes.substr(lp + 1U, i - lp - 1U);
    std::size_t dim_idx = 0U;
    std::size_t arg_start = 0U;
    for (std::size_t j = 0U; j <= inside.size(); ++j) {
        if (j == inside.size() || inside[j] == ',') {
            if (dim_idx >= dims_out.size())
                return false;
            std::uint32_t v = 0U;
            if (!parse_integer(inside.substr(arg_start, j - arg_start), v))
                return false;
            dims_out[dim_idx++] = v;
            arg_start = j + 1U;
        }
    }
    if (dim_idx != 3U)
        return false;
    attr_lo_out = found - 1U;
    attr_hi_out = i + 1U;
    return true;
}

class MeshNumthreadsOver128 : public Rule {
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
        for (const std::string_view stage_attr : {"\"mesh\"", "\"amplification\""}) {
            std::size_t pos = 0U;
            while (pos < bytes.size()) {
                const auto found = bytes.find(stage_attr, pos);
                if (found == std::string_view::npos)
                    break;
                std::array<std::uint32_t, 3> dims{0U, 0U, 0U};
                std::size_t attr_lo = 0U;
                std::size_t attr_hi = 0U;
                if (parse_numthreads_after(bytes, found, dims, attr_lo, attr_hi)) {
                    const std::uint32_t total = dims[0] * dims[1] * dims[2];
                    if (total > k_cap) {
                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Error;
                        diag.primary_span =
                            Span{.source = tree.source_id(),
                                 .bytes = ByteSpan{static_cast<std::uint32_t>(attr_lo),
                                                   static_cast<std::uint32_t>(attr_hi)}};
                        diag.message = std::string{"mesh / amplification `[numthreads("} +
                                       std::to_string(dims[0]) + ", " + std::to_string(dims[1]) +
                                       ", " + std::to_string(dims[2]) + ")]` total " +
                                       std::to_string(total) +
                                       " exceeds the D3D12 cap of 128 -- PSO creation will return "
                                       "`E_INVALIDARG`";
                        ctx.emit(std::move(diag));
                    }
                }
                pos = found + 1U;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_mesh_numthreads_over_128() {
    return std::make_unique<MeshNumthreadsOver128>();
}

}  // namespace hlsl_clippy::rules
