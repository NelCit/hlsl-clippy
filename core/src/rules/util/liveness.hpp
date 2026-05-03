// Liveness analysis over the Phase 4 control-flow graph
// (ADR 0017 sub-phase 7b.1).
//
// Backward-dataflow fixed-point iteration computing per-basic-block
// `live_in` / `live_out` AST-level local-variable name sets. The analysis
// re-walks the source's tree-sitter root to extract def/use sets per
// basic block (the CFG storage records block spans only -- it does not
// retain per-statement node handles across the public API), then runs
// the standard:
//
//   live_in[B]  = use[B] U (live_out[B] - def[B])
//   live_out[B] = U live_in[succ] for every successor
//
// to a fixed point. Used by Phase 7 rules that ask "what locals are live
// across this `TraceRay` call?" / "is the result of this assignment dead
// at the end of the block?" and by the AST-level register-pressure
// heuristic (`register_pressure_ast.{hpp,cpp}`).
//
// This header is private to `core` -- rules `#include "rules/util/..."`,
// the public surface stays free of CFG-internal types. The analysis is
// best-effort:
//   * Variable identity is tracked by name only (no scope-aware shadowing).
//   * Function calls are treated as a full kill / full re-gen barrier:
//     all in-flight locals stay live across an opaque callee.
//   * Subscript / field-access on the LHS of an assignment is treated as
//     a use of the receiver plus the index (rather than a def of an
//     element) because we do not track per-element liveness.
//   * Type identifiers, structure tags, and call-target names are never
//     treated as variable uses.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy {
class AstTree;  // forward declaration; defined in `parser_internal.hpp`
}

namespace shader_clippy::util {

/// Per-block live-variable summary. Keys are AST-level identifier names
/// extracted from the source; values are sorted vectors of names so that
/// callers can iterate cheaply and so that the result is deterministic
/// across builds. The pair vectors are themselves sorted by the
/// `BasicBlockId.raw()` value of their key, so a `std::lower_bound` style
/// lookup is O(log N) without requiring a separate indirection map.
struct LivenessInfo {
    /// `(block, sorted live-in names)` keyed by `BasicBlockId.raw()`.
    std::vector<std::pair<BasicBlockId, std::vector<std::string>>> live_in_per_block;
    /// `(block, sorted live-out names)` keyed by `BasicBlockId.raw()`.
    std::vector<std::pair<BasicBlockId, std::vector<std::string>>> live_out_per_block;

    /// Lookup helper: which AST-level locals are live at the entry of
    /// `block`? Returns an empty span when the block has no liveness info
    /// (i.e. the block belongs to a function the analysis did not visit,
    /// or `block` is the default-constructed sentinel id).
    [[nodiscard]] std::span<const std::string> live_in_at(BasicBlockId block) const noexcept;

    /// Lookup helper: which AST-level locals are live at the exit of
    /// `block`? Same emptiness contract as `live_in_at`.
    [[nodiscard]] std::span<const std::string> live_out_at(BasicBlockId block) const noexcept;
};

/// Build a `LivenessInfo` from `cfg`. The CFG must already be constructed;
/// the public `ControlFlowInfo` carries an internal `CfgImpl` shared_ptr
/// keep-alive. `tree` is the same parsed tree-sitter tree the orchestrator
/// passed into `CfgEngine::build_with_tree` -- we re-walk it to extract
/// def / use sets keyed by the `(SourceId, ByteSpan)` pairs the CFG
/// stores per block.
///
/// Returns an empty `LivenessInfo` when the CFG is empty (no functions,
/// missing impl handle) or when the tree's source id does not match any
/// block in the CFG.
[[nodiscard]] LivenessInfo compute_liveness(const ControlFlowInfo& cfg, const AstTree& tree);

}  // namespace shader_clippy::util
