// Iterative dominator-tree construction (Cooper / Harvey / Kennedy 2001).
// Walks the per-function CFG in reverse-postorder, fixed-pointing the idom
// table until no block's parent changes. Convergence is guaranteed
// (idom[b] only ever moves "up" in the postorder lattice toward the entry).

#include "control_flow/dominators.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "control_flow/cfg_storage.hpp"

namespace hlsl_clippy::control_flow {

namespace {

constexpr std::uint32_t k_undefined = static_cast<std::uint32_t>(-1);

/// Reverse-postorder DFS starting at the entry block. Used to drive the
/// fixed-point iteration in dominator order.
void compute_reverse_postorder(const CfgFunction& fn, std::vector<std::uint32_t>& rpo) {
    const auto block_count = fn.blocks.size();
    rpo.clear();
    rpo.reserve(block_count);
    if (block_count == 0U) {
        return;
    }
    std::vector<bool> seen(block_count, false);
    std::vector<std::uint32_t> order;
    order.reserve(block_count);

    // Iterative DFS with manual two-pass post-order tracking.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> stack;
    stack.emplace_back(0U, 0U);
    seen[0] = true;
    while (!stack.empty()) {
        auto& [node, succ_index] = stack.back();
        if (node >= fn.blocks.size()) {
            stack.pop_back();
            continue;
        }
        const auto& successors = fn.blocks[node].successors;
        if (succ_index < successors.size()) {
            const auto next = successors[succ_index];
            ++succ_index;
            if (next < block_count && !seen[next]) {
                seen[next] = true;
                stack.emplace_back(next, 0U);
            }
        } else {
            order.push_back(node);
            stack.pop_back();
        }
    }
    // Reverse to get RPO.
    rpo.assign(order.rbegin(), order.rend());
}

/// Intersect-along-dominator-tree helper from CHK 2001 §3. Walks both
/// `b1` and `b2` upward via `idom` until they meet; uses the postorder
/// numbers to decide which side to advance.
[[nodiscard]] std::uint32_t intersect(std::uint32_t b1,
                                      std::uint32_t b2,
                                      const std::vector<std::uint32_t>& idom,
                                      const std::vector<std::uint32_t>& postorder_num) {
    while (b1 != b2) {
        while (postorder_num[b1] < postorder_num[b2]) {
            if (idom[b1] == k_undefined) {
                return k_undefined;
            }
            const auto next = idom[b1];
            if (next == b1) {
                break;  // entry; can't climb further on this side.
            }
            b1 = next;
        }
        while (postorder_num[b2] < postorder_num[b1]) {
            if (idom[b2] == k_undefined) {
                return k_undefined;
            }
            const auto next = idom[b2];
            if (next == b2) {
                break;
            }
            b2 = next;
        }
        if (postorder_num[b1] == postorder_num[b2] && b1 != b2) {
            // Two distinct entries both at the same postorder level --
            // shouldn't happen for a connected CFG; bail out.
            return k_undefined;
        }
    }
    return b1;
}

/// Compute the predecessor list for each block. We don't store predecessors
/// in `BasicBlock` because dominator construction is the only consumer.
void compute_predecessors(const CfgFunction& fn, std::vector<std::vector<std::uint32_t>>& preds) {
    preds.assign(fn.blocks.size(), {});
    for (std::uint32_t b = 0; b < fn.blocks.size(); ++b) {
        for (const auto succ : fn.blocks[b].successors) {
            if (succ < fn.blocks.size()) {
                preds[succ].push_back(b);
            }
        }
    }
}

}  // namespace

void compute_dominators(CfgFunction& fn) {
    if (fn.error_skipped) {
        if (fn.idom.size() != fn.blocks.size()) {
            fn.idom.assign(fn.blocks.size(), 0U);
        }
        return;
    }
    const auto block_count = fn.blocks.size();
    if (block_count == 0U) {
        fn.idom.clear();
        return;
    }

    fn.idom.assign(block_count, k_undefined);
    fn.idom[0] = 0U;  // entry dominates itself

    std::vector<std::uint32_t> rpo;
    compute_reverse_postorder(fn, rpo);

    // Map block id -> postorder number (higher == closer to entry in
    // postorder traversal). CHK uses this to decide which side to climb in
    // intersect().
    std::vector<std::uint32_t> postorder_num(block_count, 0U);
    for (std::uint32_t i = 0; i < rpo.size(); ++i) {
        postorder_num[rpo[i]] = static_cast<std::uint32_t>(rpo.size() - 1U - i);
    }
    // Unreachable blocks get postorder number 0; they won't participate in
    // the iteration because they never appear in `rpo`.

    std::vector<std::vector<std::uint32_t>> preds;
    compute_predecessors(fn, preds);

    bool changed = true;
    while (changed) {
        changed = false;
        // Iterate RPO except the entry (rpo[0]).
        for (std::size_t i = 1; i < rpo.size(); ++i) {
            const auto b = rpo[i];
            std::uint32_t new_idom = k_undefined;
            for (const auto p : preds[b]) {
                if (fn.idom[p] == k_undefined) {
                    continue;  // predecessor not yet processed
                }
                if (new_idom == k_undefined) {
                    new_idom = p;
                } else {
                    const auto intersected = intersect(p, new_idom, fn.idom, postorder_num);
                    if (intersected != k_undefined) {
                        new_idom = intersected;
                    }
                }
            }
            if (new_idom != k_undefined && fn.idom[b] != new_idom) {
                fn.idom[b] = new_idom;
                changed = true;
            }
        }
    }

    // Unreachable blocks: leave idom == k_undefined. The dominates_query
    // helper in cfg_storage.cpp interprets this as "only the entry
    // trivially dominates" by walking the parent chain.
    for (auto& parent : fn.idom) {
        if (parent == k_undefined) {
            parent = 0U;  // fall back to entry-as-parent; harmless for
                          // unreachable blocks since rules never query them.
        }
    }
}

void compute_all_dominators(CfgStorage& storage) {
    // Skip sentinel function 0.
    for (std::size_t i = 1; i < storage.functions.size(); ++i) {
        compute_dominators(storage.functions[i]);
    }
}

}  // namespace hlsl_clippy::control_flow
