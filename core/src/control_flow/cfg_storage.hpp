// Internal CFG storage -- the "fat" per-source representation that backs
// `CfgImpl`, `UniformityImpl`, and the engine's per-function dominator
// trees. Held inside `core/src/control_flow/` and never exposed across the
// public API.
//
// Block ids are dense `std::uint32_t`s indexed into `blocks`. The first
// block id reserved for each function is `BasicBlockId{first}`; a function
// occupies `[first, first + count)` ids exclusively. Function 0 is invalid /
// sentinel; function ids start at 1 so a default-constructed `BasicBlockId`
// (raw value 0) never lands inside a real function.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::control_flow {

/// One basic block in the CFG. Successors are dense indices into the
/// owning function's block range; predecessors are recomputed on demand for
/// dominator construction.
struct BasicBlock {
    /// Source byte-span covered by the block. The first block of a function
    /// covers the function declaration; subsequent blocks cover the body
    /// statements they were split out of.
    Span span;
    /// Indices into `CfgFunction::blocks` (function-local).
    std::vector<std::uint32_t> successors;
    /// True when the block ends with (or contains) a barrier intrinsic call:
    /// `GroupMemoryBarrier*` or `DeviceMemoryBarrier*`. Used by
    /// `barrier_between` queries.
    bool contains_barrier = false;
    /// True when the block ends with a `discard` or `clip` statement. Used
    /// by helper-lane analysis (Phase 4 follow-ups; not consumed in 4a).
    bool contains_discard = false;
    /// True when the block ends with a `return` statement (explicit exit).
    bool is_exit = false;
    /// True when the source subtree this block was built from contained at
    /// least one ERROR node. The orchestrator skips CFG-stage rule
    /// dispatch on functions whose entry block carries this flag.
    bool tainted_by_error = false;
};

/// One function in the source. Holds its own block table + dominator-tree
/// parent vector (filled by `dominators.cpp`).
struct CfgFunction {
    /// Function-declaration span (used for `entry_span` rendering and for
    /// the `clippy::cfg-skip` diagnostic anchor).
    Span declaration_span;
    /// Function name as it appears in source. Empty when the AST did not
    /// surface a named declarator (e.g. anonymous lambdas in HLSL2021).
    std::string name;
    /// Function-local blocks. `blocks[0]` is always the entry block.
    std::vector<BasicBlock> blocks;
    /// Dominator-tree parent for each block. `idom[0]` is the function's own
    /// entry index (i.e., the entry dominates itself). Filled by
    /// `compute_dominators()`.
    std::vector<std::uint32_t> idom;
    /// True when this function was skipped due to ERROR-node taint. The
    /// `blocks` vector still holds a single sentinel entry block so
    /// `BasicBlockId` lookups remain valid; the `tainted_by_error` flag on
    /// that block is set so callers can detect the skip.
    bool error_skipped = false;
};

/// Whole-source CFG storage. Maps function id -> CfgFunction. Function ids
/// are 1-based; `functions[0]` is a sentinel (default-constructed) so a
/// default `BasicBlockId{0}` never lands inside a real function.
struct CfgStorage {
    SourceId source;
    std::vector<CfgFunction> functions;

    /// Reverse index: which function does this `BasicBlockId` belong to?
    /// Computed as the block id is allocated; queried by reach / dominate /
    /// barrier helpers.
    std::vector<std::uint32_t> block_to_function;

    /// Reverse index: function-local block index for a given global
    /// `BasicBlockId`. Lined up 1:1 with `block_to_function`.
    std::vector<std::uint32_t> block_to_local;

    /// Reverse index: byte-offset -> BasicBlockId. Built incrementally as
    /// the builder emits blocks. Used by uniformity / helper-lane queries
    /// that accept a `Span` from rule code and want to find the enclosing
    /// block. The map is `[lo, hi) -> raw block id`; lookups are linear in
    /// the worst case (we don't bother with an interval tree at this
    /// volume).
    std::vector<std::pair<Span, std::uint32_t>> span_to_block;

    /// Allocate a new global `BasicBlockId` belonging to `function_id` with
    /// the given function-local index. Returns the raw id.
    [[nodiscard]] std::uint32_t allocate_block_id(std::uint32_t function_id,
                                                  std::uint32_t local_index);

    /// Look up the global `BasicBlockId` for `(function_id, local_index)`.
    /// Returns 0 if the function or local index is out of range.
    [[nodiscard]] std::uint32_t global_id(std::uint32_t function_id,
                                          std::uint32_t local_index) const noexcept;
};

/// Engine-internal CFG impl that backs the public `CfgImpl` forward
/// declaration. Holding it via `std::shared_ptr<const CfgImpl>` inside
/// `CfgInfo` keeps the public type cheap to copy.
struct CfgImplData {
    std::shared_ptr<CfgStorage> storage;
};

/// Uniformity payload keyed by `(SourceId, byte_lo, byte_hi)`. Populated by
/// `uniformity_analyzer.cpp` from divergence-seeded taint propagation.
struct UniformityImplData {
    std::shared_ptr<CfgStorage> storage;
    /// Map from packed `(byte_lo << 32 | byte_hi)` to `Uniformity`. Lookup
    /// is O(log N) via `std::unordered_map`. The packed key trades space
    /// for query speed; spans never exceed 32 bits in practice.
    std::unordered_map<std::uint64_t, Uniformity> expr_uniformity;
    std::unordered_map<std::uint64_t, Uniformity> branch_uniformity;
};

}  // namespace hlsl_clippy::control_flow

namespace hlsl_clippy {

/// Concrete struct backing the public `CfgImpl` forward declaration.
struct CfgImpl {
    control_flow::CfgImplData data;
};

/// Concrete struct backing the public `UniformityImpl` forward declaration.
struct UniformityImpl {
    control_flow::UniformityImplData data;
};

}  // namespace hlsl_clippy
