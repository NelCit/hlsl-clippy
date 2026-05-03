// Divergent-source taint helpers + uniformity convenience wrappers for
// Phase 4 control-flow rules (ADR 0013 sub-phase 4b).
//
// These thin wrappers translate the raw `Uniformity` lattice values returned
// by the oracle (`Unknown` / `Uniform` / `Divergent` / `LoopInvariant`) into
// the boolean predicates Phase 4 rules actually want to ask. They live next
// to `cfg_query.hpp` under `core/src/rules/util/` so a single
// `#include "rules/util/uniformity.hpp"` is sufficient for the divergence
// and wave-intrinsic rule packs (ADR 0013 sub-phase 4c packs A--E).
//
// All helpers are pure-functional and return `false` (or the conservative
// answer) when the underlying oracle is uninitialised or the span is unknown
// to it. Rules treat that as "no signal, do not fire".

#pragma once

#include <string_view>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules::util {

/// True when the oracle classifies `expr_span` as `Uniformity::Uniform`.
/// Returns `false` for `LoopInvariant` (use `is_loop_invariant` for that
/// case) and for `Unknown` / `Divergent`. Callers that want "uniform OR
/// loop-invariant" should compose the two predicates.
[[nodiscard]] bool is_uniform(const ControlFlowInfo& cfg, Span expr_span) noexcept;

/// True when the oracle classifies `expr_span` as `Uniformity::Divergent`.
/// Returns `false` for `Unknown` -- conservative against false-positive
/// firings on unanalysed expressions. Rules that want "possibly divergent"
/// must check `Unknown` themselves via `cfg.uniformity.of_expr(...)`.
[[nodiscard]] bool is_divergent(const ControlFlowInfo& cfg, Span expr_span) noexcept;

/// True when `expr_span` is uniform with respect to its enclosing loop
/// (oracle returns `LoopInvariant`). Returns `false` when the expression is
/// not inside a loop or when the oracle has no entry for the span.
[[nodiscard]] bool is_loop_invariant(const ControlFlowInfo& cfg, Span expr_span) noexcept;

/// True when the branch statement at `branch_stmt_span` has a divergent
/// condition. Returns `false` when the span does not name a tracked branch
/// statement, when the condition is uniform, or when classification is
/// `Unknown`. Combines `cfg.uniformity.of_branch(...)` with the divergence
/// check rules typically want.
[[nodiscard]] bool divergent_branch(const ControlFlowInfo& cfg, Span branch_stmt_span) noexcept;

/// True when `sv_semantic_name` names an HLSL system-value semantic that is
/// inherently divergent across lanes (`SV_DispatchThreadID`,
/// `SV_GroupThreadID`, `SV_GroupIndex`, `SV_VertexID`, `SV_InstanceID`,
/// `SV_PrimitiveID`, `SV_SampleIndex`). Used by rules that emit on a
/// system-value-load span when the load flows into a resource index without
/// going through the full taint pass first; the predicate matches the seed
/// list `uniformity_analyzer.cpp` uses.
[[nodiscard]] bool is_inherently_divergent_semantic(std::string_view sv_semantic_name) noexcept;

}  // namespace shader_clippy::rules::util
