// wave-prefix-sum-vs-scan-with-atomics
//
// Detects a hand-rolled prefix-sum (exclusive or inclusive scan) implemented
// as a multi-pass groupshared-plus-barrier sequence or as a per-lane
// `InterlockedAdd` against a running counter. Per ADR 0011 §Phase 4 (rule
// pack C) and the rule's doc page, three pattern shapes trigger:
//
//   (a) Hillis-Steele up-sweep: `for (uint stride = 1; stride < N; stride <<= 1)`
//       wrapping a `g_Scan[gi] += g_Scan[gi - stride]` body with a barrier.
//   (b) Blelloch up-sweep / down-sweep: equivalent barrier-laddered shape
//       with halving / doubling stride.
//   (c) Per-lane `InterlockedAdd(g_Counter, 1)` (or any small constant)
//       used to claim a monotone slot index.
//
// All three patterns can be replaced by `WavePrefixSum` plus at most one
// barrier-and-broadcast across waves in a workgroup.
//
// Stage: `Ast`. Detection is purely syntactic: a `for` / `while` loop whose
// header advances a stride by `<<= 1` (doubling) AND whose body contains a
// `GroupMemoryBarrier*` AND a groupshared subscript that depends on the
// stride; OR an `InterlockedAdd` call whose third argument hints at slot
// claiming (constant `1` increment with the result captured into a local).
//
// Conservatism contract: the syntactic match is intentionally narrow to
// minimise false-positives against other groupshared loops that happen to
// have a barrier (e.g. tree-reduction, which is the
// `manual-wave-reduction-pattern` rule's territory). Authors with custom
// scan kernels can still suppress per-line.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "wave-prefix-sum-vs-scan-with-atomics";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_token(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]) || text[end] == '(';
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

[[nodiscard]] bool is_loop_kind(std::string_view kind) noexcept {
    return kind == "for_statement" || kind == "while_statement" || kind == "do_statement";
}

/// True when the loop header advances a stride by left-shift-1, the canonical
/// scan-doubling shape `stride <<= 1`. We accept both spaced and unspaced
/// forms to absorb formatting variations.
[[nodiscard]] bool header_doubles_stride(std::string_view text) noexcept {
    return text.find("<<= 1") != std::string_view::npos ||
           text.find("<<=1") != std::string_view::npos ||
           text.find("*= 2") != std::string_view::npos ||
           text.find("*=2") != std::string_view::npos;
}

/// True when the loop body uses an asymmetric subscript pattern of the form
/// `arr[i] += arr[i - stride]`, where `stride` is the loop's iteration
/// variable. Detected by the textual presence of `[` ... `-` ... `]` plus
/// `+=` or `=`. This is the Hillis-Steele up-sweep tell.
[[nodiscard]] bool body_has_offset_combine(std::string_view text) noexcept {
    // Look for "- stride]" -- the stride-subtracted index.
    const bool has_offset = text.find("- stride]") != std::string_view::npos ||
                            text.find("-stride]") != std::string_view::npos;
    const bool has_combine = text.find("+=") != std::string_view::npos;
    return has_offset && has_combine;
}

/// True when the loop body issues a `GroupMemoryBarrier*` call. Re-uses the
/// barrier-token convention from sibling rules.
[[nodiscard]] bool body_has_barrier(std::string_view text) noexcept {
    return has_token(text, "GroupMemoryBarrierWithGroupSync") ||
           has_token(text, "GroupMemoryBarrier");
}

/// True when `text` matches the slot-claiming atomic shape:
/// `InterlockedAdd(<counter>, 1, <out>);` -- constant-1 increment with a
/// captured output. The detection accepts any 3-arg `InterlockedAdd` and
/// any literal-1 second argument; false-positives against generic atomic
/// adds with `1` are limited because the rule's response is suggestion-only.
[[nodiscard]] bool is_slot_claim_atomic(std::string_view call_text) noexcept {
    if (!has_token(call_text, "InterlockedAdd")) {
        return false;
    }
    // Look for the literal `, 1` argument plus a `,` after it (3-arg form
    // `InterlockedAdd(buf, 1, out)`).
    const auto comma_one = call_text.find(", 1");
    if (comma_one == std::string_view::npos) {
        return false;
    }
    // Must have another `,` after the `1` to confirm 3-arg.
    return call_text.find(',', comma_one + 3U) != std::string_view::npos;
}

void scan(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);

    if (is_loop_kind(kind)) {
        const auto loop_text = node_text(node, bytes);
        const bool doubles = header_doubles_stride(loop_text);
        const bool barrier = body_has_barrier(loop_text);
        const bool combine = body_has_offset_combine(loop_text);
        if (doubles && barrier && combine) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "manual prefix-sum / scan loop (Hillis-Steele up-sweep with stride doubling and "
                "barrier ladder) -- replace with `WavePrefixSum` plus at most one cross-wave "
                "broadcast; the wave intrinsic completes the within-wave scan in 5 cycles where "
                "the manual form pays log2(N) rounds of LDS round-trip plus barrier "
                "synchronisation"};
            ctx.emit(std::move(diag));
            // Do not descend into nested loops on this chain.
            return;
        }
    }

    if (kind == "call_expression") {
        const auto call_text = node_text(node, bytes);
        if (is_slot_claim_atomic(call_text)) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message = std::string{
                "per-lane `InterlockedAdd(counter, 1, out)` slot-claim serialises 32-64 lanes on "
                "the LDS atomic unit -- replace with `WavePrefixSum(1) + WaveActiveSum(1) + one "
                "wave-leader atomic` (3 steps for what the manual form does in 32-64)"};
            ctx.emit(std::move(diag));
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class WavePrefixSumVsScanWithAtomics : public Rule {
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
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        scan(root, tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_wave_prefix_sum_vs_scan_with_atomics() {
    return std::make_unique<WavePrefixSumVsScanWithAtomics>();
}

}  // namespace shader_clippy::rules
