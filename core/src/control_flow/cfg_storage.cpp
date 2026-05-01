// Implementation of `CfgStorage` allocators + the public `CfgInfo` /
// `UniformityOracle` accessors declared in
// `core/include/hlsl_clippy/control_flow.hpp`. Pure value-type accessors --
// no tree-sitter dependency. The actual reach / dominate / barrier work
// happens in `dominators.cpp` (parents) and here (graph queries).

#include "control_flow/cfg_storage.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "hlsl_clippy/control_flow.hpp"

namespace hlsl_clippy::control_flow {

std::uint32_t CfgStorage::allocate_block_id(std::uint32_t function_id, std::uint32_t local_index) {
    // Block ids are dense + global; index 0 reserved for "invalid".
    const auto raw = static_cast<std::uint32_t>(block_to_function.size() + 1U);
    block_to_function.push_back(function_id);
    block_to_local.push_back(local_index);
    return raw;
}

std::uint32_t CfgStorage::global_id(std::uint32_t function_id,
                                    std::uint32_t local_index) const noexcept {
    // Linear scan is fine: blocks per function are O(n) and we only call
    // this during builder bookkeeping, not in hot rule queries.
    for (std::size_t i = 0; i < block_to_function.size(); ++i) {
        if (block_to_function[i] == function_id && block_to_local[i] == local_index) {
            return static_cast<std::uint32_t>(i + 1U);
        }
    }
    return 0U;
}

namespace {

/// Locate the function id + local block index for a global `BasicBlockId`
/// raw value. Returns `{0, 0}` when the id is invalid.
[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> resolve(const CfgStorage& storage,
                                                              std::uint32_t raw) noexcept {
    if (raw == 0U || raw > storage.block_to_function.size()) {
        return {0U, 0U};
    }
    const auto idx = raw - 1U;
    return {storage.block_to_function[idx], storage.block_to_local[idx]};
}

/// BFS over function-local successors. Returns true when `to_local` is
/// reachable from `from_local` along any path. The visited set is a
/// `std::vector<bool>` sized to the function's block count.
[[nodiscard]] bool reachable(const CfgFunction& fn,
                             std::uint32_t from_local,
                             std::uint32_t to_local) noexcept {
    if (from_local >= fn.blocks.size() || to_local >= fn.blocks.size()) {
        return false;
    }
    if (from_local == to_local) {
        return true;
    }
    std::vector<bool> seen(fn.blocks.size(), false);
    std::vector<std::uint32_t> stack;
    stack.push_back(from_local);
    seen[from_local] = true;
    while (!stack.empty()) {
        const auto cur = stack.back();
        stack.pop_back();
        for (const auto succ : fn.blocks[cur].successors) {
            if (succ >= fn.blocks.size() || seen[succ]) {
                continue;
            }
            if (succ == to_local) {
                return true;
            }
            seen[succ] = true;
            stack.push_back(succ);
        }
    }
    return false;
}

/// Dominator query: does `a_local` dominate `b_local`? Walks the dominator
/// tree upward from `b_local` until it hits the entry; if `a_local` appears
/// on the chain, `a` dominates `b`. The entry block (local 0) dominates
/// every reachable block by construction.
[[nodiscard]] bool dominates_query(const CfgFunction& fn,
                                   std::uint32_t a_local,
                                   std::uint32_t b_local) noexcept {
    if (a_local >= fn.blocks.size() || b_local >= fn.blocks.size()) {
        return false;
    }
    if (fn.idom.size() != fn.blocks.size()) {
        // Dominator pass didn't run (likely an error-skipped function);
        // only the entry trivially dominates anything.
        return a_local == 0U;
    }
    std::uint32_t cur = b_local;
    // Walk parents until we reach the entry (whose idom is itself by
    // convention) or stumble onto `a`.
    for (std::size_t guard = 0; guard < fn.blocks.size() + 1U; ++guard) {
        if (cur == a_local) {
            return true;
        }
        const auto parent = fn.idom[cur];
        if (parent == cur) {
            // Reached the entry (self-parent). `a` is the entry only if
            // a_local == 0; otherwise the chain didn't pass through `a`.
            return a_local == cur;
        }
        cur = parent;
    }
    return false;
}

/// "Every path from `from_local` to `to_local` passes through a barrier
/// block". Implemented by removing every barrier block from the graph and
/// asking whether `to_local` is still reachable from `from_local` (if so,
/// some barrier-free path exists, return false; otherwise return true).
[[nodiscard]] bool barrier_separates(const CfgFunction& fn,
                                     std::uint32_t from_local,
                                     std::uint32_t to_local) noexcept {
    if (from_local >= fn.blocks.size() || to_local >= fn.blocks.size()) {
        return false;
    }
    if (from_local == to_local) {
        return false;  // No path needs a barrier when source == sink.
    }
    // Per the public contract on `CfgInfo::barrier_between`, return false when
    // the sink is not reachable from the source at all. Run an unconstrained
    // reachability check first; if `to_local` cannot be reached via any path,
    // there is no path to barrier-separate and the helper must return false.
    {
        std::vector<bool> seen(fn.blocks.size(), false);
        std::vector<std::uint32_t> stack;
        stack.push_back(from_local);
        seen[from_local] = true;
        bool reachable = false;
        while (!stack.empty()) {
            const auto cur = stack.back();
            stack.pop_back();
            if (cur == to_local) {
                reachable = true;
                break;
            }
            for (const auto succ : fn.blocks[cur].successors) {
                if (succ >= fn.blocks.size() || seen[succ]) {
                    continue;
                }
                if (succ == to_local) {
                    reachable = true;
                    break;
                }
                seen[succ] = true;
                stack.push_back(succ);
            }
            if (reachable) {
                break;
            }
        }
        if (!reachable) {
            return false;
        }
    }
    if (fn.blocks[to_local].contains_barrier) {
        // Sink block itself is a barrier: trivially "passed through".
        return true;
    }
    std::vector<bool> seen(fn.blocks.size(), false);
    std::vector<std::uint32_t> stack;
    stack.push_back(from_local);
    seen[from_local] = true;
    while (!stack.empty()) {
        const auto cur = stack.back();
        stack.pop_back();
        for (const auto succ : fn.blocks[cur].successors) {
            if (succ >= fn.blocks.size() || seen[succ]) {
                continue;
            }
            if (succ == to_local) {
                // Found a barrier-free path to the sink.
                return false;
            }
            if (fn.blocks[succ].contains_barrier) {
                // This path reaches a barrier before the sink; don't follow
                // the barrier's successors looking for a barrier-free path.
                continue;
            }
            seen[succ] = true;
            stack.push_back(succ);
        }
    }
    // No barrier-free path was found.
    return true;
}

}  // namespace

}  // namespace hlsl_clippy::control_flow

namespace hlsl_clippy {

namespace {

/// Pack a `(byte_lo, byte_hi)` pair into the 64-bit key used by the
/// uniformity maps. Mirrors the encoding in `uniformity_analyzer.cpp`.
[[nodiscard]] std::uint64_t pack_span(const Span& s) noexcept {
    return (static_cast<std::uint64_t>(s.bytes.lo) << 32U) | static_cast<std::uint64_t>(s.bytes.hi);
}

}  // namespace

bool CfgInfo::reachable_from(BasicBlockId from, BasicBlockId to) const noexcept {
    if (impl == nullptr) {
        return false;
    }
    const auto& storage = impl->data.storage;
    if (storage == nullptr) {
        return false;
    }
    const auto [from_fn, from_local] = control_flow::resolve(*storage, from.raw());
    const auto [to_fn, to_local] = control_flow::resolve(*storage, to.raw());
    if (from_fn == 0U || to_fn == 0U || from_fn != to_fn) {
        return false;
    }
    return control_flow::reachable(storage->functions[from_fn], from_local, to_local);
}

bool CfgInfo::dominates(BasicBlockId a, BasicBlockId b) const noexcept {
    if (impl == nullptr) {
        return false;
    }
    const auto& storage = impl->data.storage;
    if (storage == nullptr) {
        return false;
    }
    const auto [a_fn, a_local] = control_flow::resolve(*storage, a.raw());
    const auto [b_fn, b_local] = control_flow::resolve(*storage, b.raw());
    if (a_fn == 0U || b_fn == 0U || a_fn != b_fn) {
        return false;
    }
    return control_flow::dominates_query(storage->functions[a_fn], a_local, b_local);
}

bool CfgInfo::barrier_between(BasicBlockId a, BasicBlockId b) const noexcept {
    if (impl == nullptr) {
        return false;
    }
    const auto& storage = impl->data.storage;
    if (storage == nullptr) {
        return false;
    }
    const auto [a_fn, a_local] = control_flow::resolve(*storage, a.raw());
    const auto [b_fn, b_local] = control_flow::resolve(*storage, b.raw());
    if (a_fn == 0U || b_fn == 0U || a_fn != b_fn) {
        return false;
    }
    return control_flow::barrier_separates(storage->functions[a_fn], a_local, b_local);
}

Uniformity UniformityOracle::of_expr(Span expr) const noexcept {
    if (impl == nullptr) {
        return Uniformity::Unknown;
    }
    const auto key = pack_span(expr);
    const auto it = impl->data.expr_uniformity.find(key);
    if (it == impl->data.expr_uniformity.end()) {
        return Uniformity::Unknown;
    }
    return it->second;
}

Uniformity UniformityOracle::of_branch(Span branch_stmt) const noexcept {
    if (impl == nullptr) {
        return Uniformity::Unknown;
    }
    const auto key = pack_span(branch_stmt);
    const auto it = impl->data.branch_uniformity.find(key);
    if (it == impl->data.branch_uniformity.end()) {
        return Uniformity::Unknown;
    }
    return it->second;
}

}  // namespace hlsl_clippy
