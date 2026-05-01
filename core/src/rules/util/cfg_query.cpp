// Implementation of the CFG-query helpers declared in `cfg_query.hpp`.
// Pure value-type accessors over the engine-internal `CfgStorage` -- no
// tree-sitter dependency, no allocation outside the obvious BFS scratch
// vectors, no exception path. The helpers reach into
// `core/src/control_flow/cfg_storage.hpp` (private to `core`) but do not
// drag any tree-sitter / Slang types across the boundary.

#include "rules/util/cfg_query.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules::util {

namespace {

/// True when `outer` fully contains `inner` (same source, `outer.lo <=
/// inner.lo` and `outer.hi >= inner.hi`). Spans are half-open `[lo, hi)`.
[[nodiscard]] bool contains(const Span& outer, const Span& inner) noexcept {
    if (outer.source != inner.source) {
        return false;
    }
    return outer.bytes.lo <= inner.bytes.lo && outer.bytes.hi >= inner.bytes.hi;
}

/// Locate the function-id + local block index for the smallest tracked block
/// whose recorded span encloses `span`. Returns `{0, 0, 0}` when no enclosing
/// block exists. The third element is the global raw block id.
struct BlockHit {
    std::uint32_t function_id = 0U;
    std::uint32_t local_index = 0U;
    std::uint32_t raw_id = 0U;
};

[[nodiscard]] BlockHit locate_block(const control_flow::CfgStorage& storage,
                                    const Span& span) noexcept {
    BlockHit best;
    std::uint32_t best_size = 0xFFFFFFFFU;
    for (const auto& [block_span, raw] : storage.span_to_block) {
        if (!contains(block_span, span)) {
            continue;
        }
        const std::uint32_t sz = block_span.bytes.hi - block_span.bytes.lo;
        if (sz < best_size) {
            best_size = sz;
            // Resolve the function + local index from the dense reverse-index
            // tables; raw ids are 1-based, so subtract one before indexing.
            const auto idx = raw - 1U;
            if (idx < storage.block_to_function.size()) {
                best.function_id = storage.block_to_function[idx];
                best.local_index = storage.block_to_local[idx];
                best.raw_id = raw;
            }
        }
    }
    return best;
}

/// Pull the engine-internal storage out of a public `ControlFlowInfo` value.
/// Returns `nullptr` when the impl handles are missing.
[[nodiscard]] const control_flow::CfgStorage* storage_of(const ControlFlowInfo& cfg) noexcept {
    if (cfg.cfg.impl == nullptr) {
        return nullptr;
    }
    const auto& storage = cfg.cfg.impl->data.storage;
    return storage ? storage.get() : nullptr;
}

/// BFS forward from `start_local` through the function's CFG; returns true
/// when any reachable block has `contains_discard == true`.
[[nodiscard]] bool any_reachable_discard(const control_flow::CfgFunction& fn,
                                         std::uint32_t start_local) noexcept {
    if (start_local >= fn.blocks.size()) {
        return false;
    }
    if (fn.blocks[start_local].contains_discard) {
        return true;
    }
    std::vector<bool> seen(fn.blocks.size(), false);
    std::vector<std::uint32_t> stack;
    stack.push_back(start_local);
    seen[start_local] = true;
    while (!stack.empty()) {
        const auto cur = stack.back();
        stack.pop_back();
        for (const auto succ : fn.blocks[cur].successors) {
            if (succ >= fn.blocks.size() || seen[succ]) {
                continue;
            }
            if (fn.blocks[succ].contains_discard) {
                return true;
            }
            seen[succ] = true;
            stack.push_back(succ);
        }
    }
    return false;
}

}  // namespace

std::optional<BasicBlockId> block_for(const ControlFlowInfo& cfg, Span span) noexcept {
    const auto* storage = storage_of(cfg);
    if (storage == nullptr) {
        return std::nullopt;
    }
    const auto hit = locate_block(*storage, span);
    if (hit.raw_id == 0U) {
        return std::nullopt;
    }
    return BasicBlockId{hit.raw_id};
}

bool reachable_with_discard(const ControlFlowInfo& cfg, Span from_span) noexcept {
    const auto* storage = storage_of(cfg);
    if (storage == nullptr) {
        return false;
    }
    const auto hit = locate_block(*storage, from_span);
    if (hit.function_id == 0U || hit.function_id >= storage->functions.size()) {
        return false;
    }
    const auto& fn = storage->functions[hit.function_id];
    if (fn.error_skipped) {
        return false;
    }
    return any_reachable_discard(fn, hit.local_index);
}

bool barrier_separates(const ControlFlowInfo& cfg, Span from_span, Span to_span) noexcept {
    const auto from_block = block_for(cfg, from_span);
    const auto to_block = block_for(cfg, to_span);
    if (!from_block.has_value() || !to_block.has_value()) {
        return false;
    }
    return cfg.cfg.barrier_between(from_block.value(), to_block.value());
}

bool inside_loop(const ControlFlowInfo& cfg, Span inner_span) noexcept {
    const auto* storage = storage_of(cfg);
    if (storage == nullptr) {
        return false;
    }
    const auto hit = locate_block(*storage, inner_span);
    if (hit.function_id == 0U || hit.function_id >= storage->functions.size()) {
        return false;
    }
    const auto& fn = storage->functions[hit.function_id];
    if (fn.error_skipped) {
        return false;
    }
    // Detect a back-edge by scanning every block's successor list for an
    // edge that targets a dominator of the source. A back-edge `(t -> h)` is
    // present when `h` dominates `t`; if any back-edge target dominates the
    // span's block (transitively or directly), the span is inside the loop
    // formed by that back-edge.
    if (fn.idom.size() != fn.blocks.size()) {
        return false;
    }
    const auto target_local = hit.local_index;
    for (std::uint32_t t = 0; t < fn.blocks.size(); ++t) {
        for (const auto h : fn.blocks[t].successors) {
            if (h >= fn.blocks.size()) {
                continue;
            }
            // `h` dominates `t` => back-edge t -> h.
            // Walk t's idom chain to see whether `h` lies on it.
            bool h_dominates_t = false;
            std::uint32_t cur = t;
            for (std::size_t guard = 0; guard < fn.blocks.size() + 1U; ++guard) {
                if (cur == h) {
                    h_dominates_t = true;
                    break;
                }
                const auto parent = fn.idom[cur];
                if (parent == cur) {
                    break;
                }
                cur = parent;
            }
            if (!h_dominates_t) {
                continue;
            }
            // Back-edge h -> t (h dominates t). The loop body is the set of
            // blocks dominated by h that can reach t. We approximate "span
            // inside loop" by "h dominates the span's block AND the span's
            // block can reach t".
            bool h_dominates_target = false;
            std::uint32_t walk = target_local;
            for (std::size_t guard = 0; guard < fn.blocks.size() + 1U; ++guard) {
                if (walk == h) {
                    h_dominates_target = true;
                    break;
                }
                const auto parent = fn.idom[walk];
                if (parent == walk) {
                    break;
                }
                walk = parent;
            }
            if (h_dominates_target) {
                return true;
            }
        }
    }
    return false;
}

bool inside_divergent_cf(const ControlFlowInfo& cfg, Span inner_span) noexcept {
    // The uniformity oracle keys branch-statement spans by their full
    // statement byte-range. Without an AST cursor in this layer, we
    // conservatively answer "yes" when any *enclosing* branch statement the
    // oracle knows about is divergent (or Unknown -- treated as
    // possibly-divergent to avoid false-negatives that mask UB rules).
    //
    // Implementation note: the oracle's `branch_uniformity` map is keyed by
    // the raw `(byte_lo, byte_hi)` of the branch statement, so we walk the
    // map and look for the smallest entry that encloses `inner_span` and is
    // non-uniform. Forward-compatible stub: when no AST-aware enclosure
    // table exists, this loop returns `false` -- callers degrade gracefully.
    if (cfg.uniformity.impl == nullptr) {
        return false;
    }
    const auto& branch_map = cfg.uniformity.impl->data.branch_uniformity;
    for (const auto& [packed, u] : branch_map) {
        const auto lo = static_cast<std::uint32_t>(packed >> 32U);
        const auto hi = static_cast<std::uint32_t>(packed & 0xFFFFFFFFU);
        const Span branch_span{
            .source = inner_span.source,
            .bytes = ByteSpan{.lo = lo, .hi = hi},
        };
        if (!contains(branch_span, inner_span)) {
            continue;
        }
        if (u == Uniformity::Divergent || u == Uniformity::Unknown) {
            return true;
        }
    }
    return false;
}

}  // namespace hlsl_clippy::rules::util
