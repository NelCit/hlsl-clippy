// gather-cmp-vs-manual-pcf
//
// Detects an unrolled 2x2 PCF pattern -- four `SampleCmp` / `SampleCmpLevelZero`
// calls on the same texture and sampler with neighbouring offsets -- that
// would collapse to one `GatherCmp` / `GatherCmpRed` call plus filter-weight
// math. The hardware-direct gather path issues four texel comparisons in one
// TMU instruction.
//
// Detection plan: AST. Count consecutive `SampleCmp` / `SampleCmpLevelZero`
// calls inside one function body. When >= 4 calls share the same texture
// identifier (the receiver of the dot) within a 256-byte source window,
// emit. The 4-call threshold is conservative; manual PCF kernels typically
// have exactly 4 (2x2) or 9 (3x3) calls.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "gather-cmp-vs-manual-pcf";
constexpr std::string_view k_category = "texture";
constexpr std::size_t k_window_bytes = 256U;
constexpr std::size_t k_min_calls = 4U;

class GatherCmpVsManualPcf : public Rule {
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
        // Collect all `<id>.SampleCmp[LevelZero]?(` call positions.
        struct Call {
            std::string receiver;
            std::size_t pos = 0U;
        };
        std::vector<Call> calls;
        std::size_t pos = 0U;
        while (pos < bytes.size()) {
            const auto found = bytes.find(".SampleCmp", pos);
            if (found == std::string_view::npos)
                break;
            const std::size_t after = found + std::string_view{".SampleCmp"}.size();
            // Accept `.SampleCmp(`, `.SampleCmpLevelZero(`, and `.SampleCmpLevel(`.
            std::size_t k = after;
            while (k < bytes.size() && is_id_char(bytes[k]))
                ++k;
            if (k >= bytes.size() || bytes[k] != '(') {
                pos = found + 1U;
                continue;
            }
            // Receiver: identifier ending at `found`.
            std::size_t end = found;
            std::size_t start = end;
            while (start > 0U && is_id_char(bytes[start - 1U]))
                --start;
            if (start == end) {
                pos = found + 1U;
                continue;
            }
            calls.push_back({std::string{bytes.substr(start, end - start)}, found});
            pos = k + 1U;
        }
        if (calls.size() < k_min_calls)
            return;
        // For each call, count neighbours within `k_window_bytes` that share
        // the receiver. If the cluster is large enough, emit on the first
        // call.
        std::vector<bool> reported(calls.size(), false);
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (reported[i])
                continue;
            std::size_t cluster_end = i;
            std::size_t count = 1U;
            for (std::size_t j = i + 1U; j < calls.size(); ++j) {
                if (calls[j].pos - calls[i].pos > k_window_bytes)
                    break;
                if (calls[j].receiver != calls[i].receiver)
                    continue;
                cluster_end = j;
                ++count;
            }
            if (count < k_min_calls)
                continue;
            for (std::size_t j = i; j <= cluster_end; ++j)
                reported[j] = true;
            const auto first = calls[i].pos + 1U;
            const auto last = calls[cluster_end].pos + std::string_view{".SampleCmp"}.size();
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Note;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(first),
                                                       static_cast<std::uint32_t>(last)}};
            diag.message = std::string{"`"} + calls[i].receiver + "` has " + std::to_string(count) +
                           " `SampleCmp*` calls in a small window -- a 2x2 / 3x3 PCF kernel "
                           "collapses to one `GatherCmp` (or `GatherCmpRed`) plus weight math, "
                           "saving 3-8 TMU issue cycles";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_gather_cmp_vs_manual_pcf() {
    return std::make_unique<GatherCmpVsManualPcf>();
}

}  // namespace shader_clippy::rules
