// Helper-lane-state queries for Phase 4 control-flow rules
// (ADR 0013 sub-phase 4b).
//
// HLSL pixel-shader semantics: when a quad lane executes `discard` (or the
// older `clip(...)` form), the GPU may keep the lane alive as a "helper
// lane" so that derivatives + quad-uniform operations remain well-defined
// on the surviving lanes. Wave intrinsics dispatched from a helper lane
// produce undefined results. The Phase 4 rule pack
// `wave-intrinsic-helper-lane-hazard` (ADR 0007 §Phase 4) and the
// `clip-from-non-uniform-cf` rule (ADR 0011 §Phase 4) need to ask whether
// a program point may be reached on a path that has already executed a
// `discard`.
//
// Sub-phase 4a deferred this analysis to 4b; the engine records per-block
// `contains_discard` flags but does not yet stamp `HelperLaneState` onto
// `CfgNodeInfo`. These helpers compute the answer by walking predecessors
// of the block containing the query span and looking for any block on a
// path-from-entry that has `contains_discard == true`.
//
// Conservatism contract:
//   * When the CFG is missing or the span sits outside any tracked block,
//     `possibly_helper_lane_at` returns `false` (no signal).
//   * When the engine cannot tell which stage a function belongs to (e.g.
//     `enable_reflection = false`), `in_pixel_stage_or_unknown` returns
//     `true` -- "treat as possibly PS" per ADR 0013 §"Risks & mitigations"
//     ("conservative-true ... never *incorrect* (no false-`NotApplicable`
//     claims that would let a real bug through)").

#pragma once

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::rules::util {

/// True when `span` is reached by a path that has already executed a
/// `discard` (or `clip(...)`), so the lane may be a helper lane in pixel-
/// shader stages. The check walks the dominator tree upward from the
/// block containing `span` and returns `true` as soon as it finds a
/// block whose subtree the builder flagged with `contains_discard`.
/// Returns `false` when:
///   * the CFG impl handle is null or the span is outside every tracked
///     block;
///   * no `discard`-tagged ancestor exists on the dominator chain;
///   * the enclosing function was ERROR-skipped during CFG construction.
[[nodiscard]] bool possibly_helper_lane_at(const ControlFlowInfo& cfg, Span span) noexcept;

/// True when `cfg` was built for a function whose stage is or could be
/// pixel shader. When reflection is unavailable to confirm the stage,
/// returns `true` (conservative: per ADR 0013 §"Risks & mitigations",
/// the analyzer falls back to "treat every function as possibly-PS"
/// rather than risk a false-`NotApplicable` that would let a real
/// helper-lane bug through). The current sub-phase 4a `ControlFlowInfo`
/// does not carry stage metadata, so this helper unconditionally returns
/// `true` -- it documents the contract Phase 4 PS-only rules can rely
/// on, and tightens to a real per-function predicate once 4a's
/// helper-lane-state stamping lands.
[[nodiscard]] bool in_pixel_stage_or_unknown(const ControlFlowInfo& cfg) noexcept;

}  // namespace shader_clippy::rules::util
