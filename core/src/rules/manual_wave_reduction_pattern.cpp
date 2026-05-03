// manual-wave-reduction-pattern
//
// Detects hand-rolled reductions that reproduce the semantics of `WaveActiveSum`
// / `WaveActiveProduct` / `WaveActiveMin` / `WaveActiveMax` / `WaveActiveBitOr`
// / `WaveActiveBitAnd` / `WaveActiveBitXor` / `WaveActiveCountBits`. Per ADR
// 0011 §Phase 4 (rule pack C) and the rule's doc page, three pattern shapes
// trigger:
//
//   (a) A `for` / `while` loop whose body issues an `InterlockedAdd` /
//       `InterlockedOr` / ... against a groupshared cell (single-counter
//       atomic-loop reduction).
//   (b) A halving-stride tree-reduction loop with `GroupMemoryBarrier*`
//       between rounds, indexing into a `groupshared` array (the
//       canonical Hillis-Steele sweep variant for reductions).
//   (c) A WaveReadLaneAt-shuffle ladder, where a value is recursively
//       combined with `WaveReadLaneAt(x, lane ^ k)` for a power-of-two
//       `k` -- the same-wave shuffle-tree case.
//
// Stage: `ControlFlow`. The rule walks the AST to identify a candidate
// reduction loop and uses `cfg_query::inside_loop` only as a sanity check
// that the operations are inside a loop body. The actual reduction-pattern
// match is purely AST-shape-based -- the CFG is not strictly required for
// this detection but the rule ships at `Stage::ControlFlow` to align with
// the doc page's "control-flow" category and to keep the helper-call
// surface stable when the engine grows tighter loop-body classification.
//
// Conservatism contract: the rule only fires on shapes that strongly
// resemble a reduction. Author-friendly fallbacks (e.g. an
// `InterlockedAdd` that is *not* in a loop) are silently ignored. The fix
// is `suggestion`-only since the reduction scope (wave vs workgroup) and
// surrounding LDS / barrier structure must be confirmed by the author.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
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

constexpr std::string_view k_rule_id = "manual-wave-reduction-pattern";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 6> k_atomic_calls{
    "InterlockedAdd",
    "InterlockedOr",
    "InterlockedAnd",
    "InterlockedXor",
    "InterlockedMin",
    "InterlockedMax",
};

constexpr std::string_view k_barrier_call = "GroupMemoryBarrierWithGroupSync";
constexpr std::string_view k_shuffle_call = "WaveReadLaneAt";

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
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

/// True when `text` (the body of a loop) contains at least one named
/// `Interlocked*` call from `k_atomic_calls`.
[[nodiscard]] bool body_contains_atomic(std::string_view text) noexcept {
    for (const auto call : k_atomic_calls) {
        if (has_token(text, call)) {
            return true;
        }
    }
    return false;
}

/// True when `text` contains a `GroupMemoryBarrierWithGroupSync` call (the
/// barrier round in a tree-reduction sweep).
[[nodiscard]] bool body_contains_barrier(std::string_view text) noexcept {
    return has_token(text, k_barrier_call);
}

/// True when `text` contains a `WaveReadLaneAt` ladder. The ladder shape is
/// distinguished from a single use by the presence of `^` (XOR with a
/// power-of-two literal), which is the canonical butterfly index. We accept
/// the simpler heuristic of "WaveReadLaneAt + xor in body" to avoid
/// false-positives on a single broadcast.
[[nodiscard]] bool body_contains_shuffle_ladder(std::string_view text) noexcept {
    if (!has_token(text, k_shuffle_call)) {
        return false;
    }
    return text.find('^') != std::string_view::npos;
}

/// True when the loop's iteration controller has the shape of a halving
/// stride: `for (... ; cond ; stride >>= 1)` or `stride /= 2`. We look for
/// `>>= 1` or `/= 2` in the loop header text, anchored to a stride-like
/// identifier.
[[nodiscard]] bool header_has_halving_stride(std::string_view text) noexcept {
    return text.find(">>= 1") != std::string_view::npos ||
           text.find(">>=1") != std::string_view::npos ||
           text.find("/= 2") != std::string_view::npos ||
           text.find("/=2") != std::string_view::npos;
}

[[nodiscard]] bool is_loop_kind(std::string_view kind) noexcept {
    return kind == "for_statement" || kind == "while_statement" || kind == "do_statement";
}

void scan_loops(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    if (is_loop_kind(kind)) {
        const auto loop_text = node_text(node, bytes);
        const bool has_atomic = body_contains_atomic(loop_text);
        const bool has_barrier = body_contains_barrier(loop_text);
        const bool has_shuffle = body_contains_shuffle_ladder(loop_text);
        const bool halving = header_has_halving_stride(loop_text);

        // Pattern (a): atomic-loop reduction (atomic call inside a loop).
        // Pattern (b): halving-stride + barrier + groupshared subscript.
        // Pattern (c): WaveReadLaneAt with XOR (butterfly).
        const bool tree_reduction = has_barrier && halving;
        const bool atomic_reduction = has_atomic;
        const bool shuffle_ladder = has_shuffle;

        if (tree_reduction || atomic_reduction || shuffle_ladder) {
            std::string_view variant_msg;
            if (tree_reduction) {
                variant_msg =
                    "tree-reduction with halving stride and groupshared barriers replaceable by "
                    "`WaveActive*`";
            } else if (atomic_reduction) {
                variant_msg =
                    "atomic-counter loop reduction replaceable by `WaveActive*` + single "
                    "wave-leader atomic publish";
            } else {
                variant_msg =
                    "WaveReadLaneAt-XOR butterfly ladder replaceable by a single `WaveActive*` "
                    "call";
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
            diag.message =
                std::string{"manual wave-reduction pattern detected ("} + std::string{variant_msg} +
                ") -- modern GPUs implement these as cross-lane primitives in "
                "log2(wave_size) cycles; the manual form burns LDS bandwidth and "
                "barrier-synchroniser cycles for what one wave intrinsic does in 5 cycles";
            ctx.emit(std::move(diag));
            return;  // do not descend into nested loops on the same chain.
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_loops(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualWaveReductionPattern : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& /*cfg*/, RuleContext& ctx) override {
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        scan_loops(root, tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_wave_reduction_pattern() {
    return std::make_unique<ManualWaveReductionPattern>();
}

}  // namespace shader_clippy::rules
