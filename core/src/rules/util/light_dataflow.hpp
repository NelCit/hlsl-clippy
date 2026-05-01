// Light data-flow helpers for Phase 4 control-flow rules
// (ADR 0013 sub-phase 4b).
//
// Phase 4 rules need a handful of read-before-write / write-without-read /
// loop-invariance queries without paying for a full classical data-flow
// framework. These helpers answer just those queries -- they do not aim to
// be a general-purpose live-variable / reaching-definition analyser. The
// underlying inputs are the same `ControlFlowInfo` value type that
// `cfg_query.hpp` and `uniformity.hpp` consume.
//
// Conservatism contract: when the analysis cannot answer (CFG impl missing,
// span outside any tracked block, ERROR-tainted function), every helper
// returns `false`. Rules treat that as "no signal, do not fire". Several of
// the helpers below are forward-compatible stubs against sub-phase 4a's
// minimum-viable CFG -- they document the exact shape Phase 4 rule-pack
// agents can rely on, and tighten as the engine grows (per ADR 0013
// "Decision Outcome" point 6 + the "best-effort" contract).

#pragma once

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules::util {

/// True when a `groupshared` cell whose declaration covers `gs_decl_span`
/// is read on some CFG path before any thread writes to it. Conservative:
/// returns `false` on ERROR-tolerated CFGs, on missing impl handles, and
/// when no read/write tracking is recorded for the span.
///
/// Forward-compatible stub: sub-phase 4a's CFG records block-level barrier
/// + discard flags but does not yet maintain per-cell first-access kind.
/// This helper returns `false` until the underlying engine grows
/// `groupshared_first_access_kind` (per ADR 0013 §"Decision Outcome"
/// point 6). Rules that depend on it land in the same pack as the engine
/// extension or open-code a syntactic approximation as a fallback.
[[nodiscard]] bool groupshared_read_before_write(const ControlFlowInfo& cfg,
                                                 Span gs_decl_span) noexcept;

/// True when the variable named at `var_span` is written without any
/// subsequent read on any CFG path to function exit. Conservative:
/// returns `false` when the analysis cannot make a definite negative
/// claim (i.e. presence of a use kills the predicate).
///
/// Forward-compatible stub: sub-phase 4a's CFG does not yet record
/// per-variable use-def chains. This helper returns `false` until the
/// underlying engine grows that table; the API shape is locked so the
/// `dead-store-sv-target` and `groupshared-dead-store` rules can compile
/// against it today and tighten when the engine catches up.
[[nodiscard]] bool dead_store(const ControlFlowInfo& cfg, Span var_span) noexcept;

/// True when `expr_span` evaluates to the same value on every iteration of
/// its enclosing loop. The implementation classifies the expression via
/// the uniformity oracle and returns `true` for `Uniformity::Uniform` /
/// `Uniformity::LoopInvariant` -- both are loop-iteration-invariant by
/// construction. Returns `false` for `Divergent` / `Unknown`, and `false`
/// when the expression is not inside any loop (use `cfg_query::inside_loop`
/// to discriminate that case).
[[nodiscard]] bool loop_invariant_expr(const ControlFlowInfo& cfg, Span expr_span) noexcept;

}  // namespace hlsl_clippy::rules::util
