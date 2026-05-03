// meshlet-vertex-count-bad
//
// Detects mesh-shader meshlet vertex output counts that are either above the
// RDNA-optimal 128 cap OR not a multiple of 32 (the wave size that NVIDIA
// Turing+ and Ada prefer for meshlet-output throughput). The hard D3D12 cap
// of 256 is policed by `mesh-output-decl-exceeds-256`; this rule catches the
// "still legal but pessimal" range.
//
// Stage: Reflection. We accept the AST handed to `on_reflection` and walk
// for the same `out vertices ... arr[N]` shape `mesh-output-decl-exceeds-256`
// matches; firing on the (32 < N <= 256, N % 32 != 0) OR (N > 128) cases.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "meshlet-vertex-count-bad";
constexpr std::string_view k_category = "mesh";
constexpr std::uint32_t k_rdna_optimal_cap = 128U;
constexpr std::uint32_t k_d3d12_hard_cap = 256U;
constexpr std::uint32_t k_wave_align = 32U;

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

void scan(std::string_view bytes, std::string_view kind_kw, const AstTree& tree, RuleContext& ctx) {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(kind_kw, pos);
        if (found == std::string_view::npos)
            return;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + kind_kw.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        const auto lb = bytes.find('[', end);
        if (lb == std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto rb = bytes.find(']', lb + 1U);
        if (rb == std::string_view::npos) {
            pos = end;
            continue;
        }
        if (bytes.substr(end, lb - end).find(';') != std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto digits = trim(bytes.substr(lb + 1U, rb - lb - 1U));
        std::uint32_t v = 0U;
        bool ok = !digits.empty();
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            v = v * 10U + static_cast<std::uint32_t>(c - '0');
        }
        if (!ok) {
            pos = rb + 1U;
            continue;
        }
        // Skip values caught by mesh-output-decl-exceeds-256 (the hard 256
        // cap) -- those get a separate Error-severity diagnostic.
        if (v > k_d3d12_hard_cap) {
            pos = rb + 1U;
            continue;
        }
        const bool above_rdna = v > k_rdna_optimal_cap;
        const bool wave_misaligned = (v > 0U) && (v % k_wave_align != 0U);
        if (above_rdna || wave_misaligned) {
            std::string reason;
            if (above_rdna && wave_misaligned) {
                reason =
                    " is above the RDNA-optimal 128-vertex meshlet cap AND not a multiple "
                    "of 32 (the NVIDIA Turing+ wave-aligned meshlet count)";
            } else if (above_rdna) {
                reason =
                    " is above the RDNA-optimal 128-vertex meshlet cap -- RDNA 2/3 meshlet "
                    "throughput peaks at 128 vertices/group; above that the dispatcher "
                    "splits into multiple sub-meshlets at runtime cost";
            } else {
                reason =
                    " is not a multiple of 32 -- NVIDIA Turing+ meshlet throughput is "
                    "wave-aligned, and a fractional last wave wastes lanes on every group";
            }
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{
                .source = tree.source_id(),
                .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                  static_cast<std::uint32_t>(rb + 1U)},
            };
            diag.message = std::string{"meshlet output count "} + std::to_string(v) + reason;
            ctx.emit(std::move(diag));
        }
        pos = rb + 1U;
    }
}

class MeshletVertexCountBad : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       [[maybe_unused]] const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        // Only fire when the source has mesh-shader markers.
        if (bytes.find("outputtopology") == std::string_view::npos &&
            bytes.find("\"mesh\"") == std::string_view::npos) {
            return;
        }
        scan(bytes, "out vertices", tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_meshlet_vertex_count_bad() {
    return std::make_unique<MeshletVertexCountBad>();
}

}  // namespace shader_clippy::rules
