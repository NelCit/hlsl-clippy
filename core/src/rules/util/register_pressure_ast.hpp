// AST + reflection register-pressure heuristic over the Phase 4 CFG
// (ADR 0017 sub-phase 7b.2).
//
// Source-level estimator for "how many VGPRs live at each basic block?".
// Sums the bit-widths of every name in `LivenessInfo::live_in_at(block)`
// (rounded up to a 32-bit-VGPR slot) and produces a per-block estimate.
// Bit-widths are looked up via reflection (cbuffer field types, entry-
// point parameter types) when available; otherwise via lexeme inspection
// of the variable's declaration source span; otherwise default 32 bits.
//
// The estimator is best-effort: see ADR 0017 §"Trade-offs we accept by
// skipping the lowered-IR layer". We over-count compared to what the
// real DXIL register allocator emits because we cannot see compiler-
// introduced spills or compiler dead-code elimination. The output is
// useful as an upper bound on "places worth investigating manually" --
// rules that consume it ship at warn severity (per ADR 0016 decision
// driver "Best-effort precision").
//
// Header private to `core` -- consumers `#include "rules/util/..."`.

#pragma once

#include <cstdint>
#include <vector>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {
class AstTree;        // forward declaration; defined in `parser_internal.hpp`.
struct ReflectionInfo;  // forward declaration; defined in `reflection.hpp`.
}

namespace hlsl_clippy::util {

struct LivenessInfo;  // forward declaration; defined in `liveness.hpp`.

/// Per-basic-block VGPR estimate. `estimated_vgprs` is the sum over every
/// live-in name in the block of `ceil(bit_width / 32)`. Sorted descending
/// by `estimated_vgprs` so callers can `front()` for the worst-case
/// block in the source.
struct PressureEstimate {
    BasicBlockId block;
    std::uint32_t estimated_vgprs = 0;
};

/// Compute a per-block VGPR estimate for `cfg` using `liveness` for the
/// per-block live-value sets and `reflection` (when non-null) for type
/// bit-width lookups. `tree` is the parsed tree-sitter root of the same
/// source -- used to inspect declaration-site type lexemes for variables
/// that reflection cannot resolve (e.g. function-local `min16float`
/// temporaries that never appear in cbuffers or signatures).
///
/// `threshold` is forwarded for the convenience of consumer rules that
/// want to flag any block above it; the heuristic itself does not
/// filter -- the full per-block list is returned.
///
/// Result is sorted by `estimated_vgprs` descending. Returns an empty
/// vector when `cfg` is empty or when liveness produced no per-block
/// data (e.g. ERROR-tolerated source).
[[nodiscard]] std::vector<PressureEstimate> estimate_pressure(const ControlFlowInfo& cfg,
                                                              const LivenessInfo& liveness,
                                                              const AstTree& tree,
                                                              const ReflectionInfo* reflection,
                                                              std::uint32_t threshold);

}  // namespace hlsl_clippy::util
