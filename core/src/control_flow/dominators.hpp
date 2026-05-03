// Per-function dominator tree computation. Fills `CfgFunction::idom` so
// `CfgInfo::dominates()` queries can walk the parent chain in O(depth).
//
// Per ADR 0013, the algorithm of choice is the iterative dataflow approach
// described in Cooper / Harvey / Kennedy 2001 ("A Simple, Fast Dominance
// Algorithm"). This is intentionally NOT Lengauer-Tarjan: the CHK iterative
// algorithm is dramatically simpler to implement, has similar empirical
// performance on the small CFGs Phase 4 rules consume (typical functions
// produce 10s -- not 1000s -- of basic blocks), and dodges Lengauer-Tarjan's
// linkable-bucket / SDOM-vs-IDOM bookkeeping. The choice is documented in
// the sub-phase 4a report; if profiling shows dominator construction is a
// hot spot on real shaders, a Lengauer-Tarjan replacement is a one-file
// drop-in here.

#pragma once

#include "control_flow/cfg_storage.hpp"

namespace shader_clippy::control_flow {

/// Compute and store the immediate-dominator parent vector for every block
/// in `fn`. The entry block (local index 0) has `idom[0] == 0` by
/// convention. Functions tagged `error_skipped` are left untouched (their
/// `idom` stays as the single-element vector the builder set).
void compute_dominators(CfgFunction& fn);

/// Convenience: run `compute_dominators` on every non-sentinel function in
/// the storage.
void compute_all_dominators(CfgStorage& storage);

}  // namespace shader_clippy::control_flow
