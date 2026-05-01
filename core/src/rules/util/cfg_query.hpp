// Common CFG-query helpers for Phase 4 control-flow rules
// (ADR 0013 sub-phase 4b).
//
// These helpers sit on top of the value-type `ControlFlowInfo` declared in
// `hlsl_clippy/control_flow.hpp`. They translate rule-side `Span` values into
// `BasicBlockId` lookups, then forward to the engine-internal CFG queries
// exposed via `CfgInfo::reachable_from` / `CfgInfo::dominates` /
// `CfgInfo::barrier_between`. Rules with `stage() == Stage::ControlFlow`
// include this header from `core/src/rules/util/`; the public surface stays
// free of tree-sitter / Slang types because every accessor here operates on
// the already-materialised CFG structs.
//
// The helpers are intentionally conservative: when the underlying CFG cannot
// answer a question (impl handle null, span outside any tracked block,
// ERROR-tainted function), the helper returns `false` (or `std::nullopt` for
// the locator). Rules treat that as "no signal, do not fire".
//
// Forward-compatible stubs: a few helpers below currently rely only on what
// sub-phase 4a exposes. Where 4a does not yet record the underlying signal
// (e.g. callee-equality matching for `dispatchmesh-not-called` rule wants the
// callee identifier, not just "any successor"), the helper returns the
// conservative answer and is documented as such inline. Phase 4 rule packs
// can rely on the API shape; precision tightens as the engine grows.

#pragma once

#include <optional>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::rules::util {

/// Return the `BasicBlockId` whose recorded source span encloses `span`, or
/// `std::nullopt` when the span sits outside every tracked block in the
/// supplied `ControlFlowInfo`. The lookup is linear in the block count;
/// callers that need to repeat it across many spans should cache the result.
[[nodiscard]] std::optional<BasicBlockId> block_for(const ControlFlowInfo& cfg, Span span) noexcept;

/// True when a `discard` (or `clip`) statement is reachable on some CFG path
/// from the basic block containing `from_span` to any function-exit block in
/// the same function. Returns `false` when `from_span` cannot be located in
/// the CFG. The implementation walks successor edges and consults the
/// `contains_discard` flag the builder records on each block.
[[nodiscard]] bool reachable_with_discard(const ControlFlowInfo& cfg, Span from_span) noexcept;

/// True when every CFG path from the block containing `from_span` to the
/// block containing `to_span` passes through at least one barrier-tagged
/// block (`GroupMemoryBarrier*`, `DeviceMemoryBarrier*`, etc.). Returns
/// `false` when either span is outside the CFG, when the spans live in
/// different functions, or when at least one barrier-free path exists.
[[nodiscard]] bool barrier_separates(const ControlFlowInfo& cfg,
                                     Span from_span,
                                     Span to_span) noexcept;

/// True when `inner_span` is enclosed by a loop construct (`for` / `while` /
/// `do`). Sub-phase 4a records loop boundaries as block-edge fan-out from a
/// header block; this helper detects loop enclosure by walking the dominator
/// tree upward from `inner_span`'s block and looking for a block that has a
/// back-edge target. Returns `false` when the span is outside the CFG or
/// when no enclosing loop is found.
[[nodiscard]] bool inside_loop(const ControlFlowInfo& cfg, Span inner_span) noexcept;

/// True when `inner_span` is enclosed by a non-uniform branch (`if` /
/// `switch` / `?:`) per the uniformity oracle. The check finds the smallest
/// enclosing branch statement whose condition is `Uniformity::Divergent` (or
/// `Uniformity::Unknown` -- treated as possibly-divergent for safety).
/// Returns `false` when no enclosing branch is found, or when every
/// enclosing branch has a uniform condition.
[[nodiscard]] bool inside_divergent_cf(const ControlFlowInfo& cfg, Span inner_span) noexcept;

}  // namespace hlsl_clippy::rules::util
