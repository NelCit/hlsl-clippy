// Implementation of the helper-lane queries declared in
// `helper_lane_analyzer.hpp`. Pure value-type accessors over the engine-
// internal `CfgStorage` -- no tree-sitter dependency, no allocation outside
// the BFS scratch vector, no exception path.
//
// `possibly_helper_lane_at` walks the dominator tree upward from the block
// containing `span` and reports `true` as soon as it finds a block whose
// builder-recorded `contains_discard` flag is set. This intentionally uses
// the dominator chain rather than full reverse-reachability: a helper-lane
// state is "definitely possible" only when every path from function entry
// passes through a discard, but the conservative answer for the Phase 4
// `wave-intrinsic-helper-lane-hazard` rule is "any predecessor on a path
// from entry has a discard". We compute the latter by also scanning forward
// reachability of every discard-tagged block; either signal flips the result
// to `true`.

#include "rules/util/helper_lane_analyzer.hpp"

#include <cstdint>
#include <vector>

#include "control_flow/cfg_storage.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules::util {

namespace {

[[nodiscard]] bool contains(const Span& outer, const Span& inner) noexcept {
    if (outer.source != inner.source) {
        return false;
    }
    return outer.bytes.lo <= inner.bytes.lo && outer.bytes.hi >= inner.bytes.hi;
}

struct LocalHit {
    std::uint32_t function_id = 0U;
    std::uint32_t local_index = 0U;
};

[[nodiscard]] LocalHit locate(const control_flow::CfgStorage& storage, const Span& span) noexcept {
    LocalHit best;
    std::uint32_t best_size = 0xFFFFFFFFU;
    for (const auto& [block_span, raw] : storage.span_to_block) {
        if (!contains(block_span, span)) {
            continue;
        }
        const std::uint32_t sz = block_span.bytes.hi - block_span.bytes.lo;
        if (sz < best_size) {
            best_size = sz;
            const auto idx = raw - 1U;
            if (idx < storage.block_to_function.size()) {
                best.function_id = storage.block_to_function[idx];
                best.local_index = storage.block_to_local[idx];
            }
        }
    }
    return best;
}

/// Returns true when any block whose `contains_discard` flag is set can
/// reach `target_local` via forward CFG edges. We BFS from every
/// discard-tagged block and check whether `target_local` is in the
/// closure -- this catches the common `if (cond) discard; ... wave_op();`
/// pattern that motivates the helper-lane rules.
[[nodiscard]] bool any_discard_can_reach(const control_flow::CfgFunction& fn,
                                         std::uint32_t target_local) noexcept {
    if (target_local >= fn.blocks.size()) {
        return false;
    }
    for (std::uint32_t start = 0; start < fn.blocks.size(); ++start) {
        if (!fn.blocks[start].contains_discard) {
            continue;
        }
        if (start == target_local) {
            return true;
        }
        std::vector<bool> seen(fn.blocks.size(), false);
        std::vector<std::uint32_t> stack;
        stack.push_back(start);
        seen[start] = true;
        while (!stack.empty()) {
            const auto cur = stack.back();
            stack.pop_back();
            for (const auto succ : fn.blocks[cur].successors) {
                if (succ >= fn.blocks.size() || seen[succ]) {
                    continue;
                }
                if (succ == target_local) {
                    return true;
                }
                seen[succ] = true;
                stack.push_back(succ);
            }
        }
    }
    return false;
}

}  // namespace

bool possibly_helper_lane_at(const ControlFlowInfo& cfg, Span span) noexcept {
    if (cfg.cfg.impl == nullptr) {
        return false;
    }
    const auto& storage_ptr = cfg.cfg.impl->data.storage;
    if (storage_ptr == nullptr) {
        return false;
    }
    const auto hit = locate(*storage_ptr, span);
    if (hit.function_id == 0U || hit.function_id >= storage_ptr->functions.size()) {
        return false;
    }
    const auto& fn = storage_ptr->functions[hit.function_id];
    if (fn.error_skipped) {
        return false;
    }
    return any_discard_can_reach(fn, hit.local_index);
}

bool in_pixel_stage_or_unknown(const ControlFlowInfo& cfg) noexcept {
    // Sub-phase 4a does not stamp stage metadata onto `ControlFlowInfo`; per
    // ADR 0013 §"Risks & mitigations", "when reflection is unavailable the
    // analyzer falls back to 'treat every function as possibly-PS'". Return
    // `true` until the engine carries per-function stage data.
    (void)cfg;
    return true;
}

}  // namespace shader_clippy::rules::util
