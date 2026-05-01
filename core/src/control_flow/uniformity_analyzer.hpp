// Uniformity taint propagation -- walks the AST a second time, seeded by
// the known-divergent system values, and populates the
// `UniformityImplData` tables consumed by `UniformityOracle::of_expr` /
// `of_branch`.
//
// Per ADR 0013 §"Decision Outcome" point 4, divergent seeds include:
//   * SV_DispatchThreadID, SV_GroupThreadID, SV_GroupIndex,
//     SV_PrimitiveID, SV_VertexID, SV_InstanceID
//   * WaveGetLaneIndex(), wave-lane-id intrinsics
//   * resource indices flagged NonUniform via reflection (when reflection
//     is available)
//
// Operand flow is `Divergent ⊕ * = Divergent`; literals + cbuffer fields +
// `WaveGetLaneCount()` are uniform; loop induction variables are
// `LoopInvariant` inside their loop body. Inter-procedural fan-out is
// bounded by `cfg_inlining_depth` -- callees deeper than that are treated
// as opaque and contribute `Unknown`.

#pragma once

#include <cstdint>
#include <string_view>

#include <tree_sitter/api.h>

#include "control_flow/cfg_storage.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::control_flow {

/// Walk `root` and populate `out` with per-span uniformity classifications.
/// `reflection` may be `nullptr`; when non-null, the analyzer additionally
/// treats reflection-flagged-NonUniform resources as divergent seeds.
/// `cfg_inlining_depth` bounds the inter-procedural fan-out at which
/// callees stop being inlined.
void analyse_uniformity(::TSNode root,
                        SourceId source,
                        std::string_view bytes,
                        const ReflectionInfo* reflection,
                        std::uint32_t cfg_inlining_depth,
                        UniformityImplData& out);

}  // namespace hlsl_clippy::control_flow
