---
id: manual-wave-reduction-pattern
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# manual-wave-reduction-pattern

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

Hand-rolled reductions that reproduce the semantics of `WaveActiveSum`, `WaveActiveProduct`, `WaveActiveMin`, `WaveActiveMax`, `WaveActiveBitOr`, `WaveActiveBitAnd`, `WaveActiveBitXor`, or `WaveActiveCountBits`. Pattern shapes detected: (a) a `for` loop that accumulates per-lane values into a groupshared cell via `InterlockedAdd` (or the corresponding atomic), (b) a tree-reduction loop over a groupshared array with halving stride and barriers between rounds, and (c) a same-wave shuffle-tree implemented via `WaveReadLaneAt(x, i ^ k)` for `k = 1, 2, 4, 8, 16` ladder. All three shapes are subsumed by a single `WaveActive*` call when the reduction scope is the wave (the same-wave shuffle tree case) or by `WaveActive*` followed by an LDS atomic / barrier when the scope is the workgroup.

## Why it matters on a GPU

Every modern GPU implements wave-level reductions as a primitive on dedicated cross-lane hardware: AMD RDNA 2/3 uses DPP (Data-Parallel Primitives) and SDWA paths through the SIMD unit, completing a 32-lane reduction in 5 cycles (log₂ 32) on RDNA 3; NVIDIA Ada Lovelace exposes `__shfl_xor_sync` / shfl-tree wired into the warp shuffle network, similarly 5 cycles for a 32-lane warp; Intel Xe-HPG provides equivalent subgroup-reduce intrinsics through the vector ALU's lane-crossing path. The HLSL `WaveActive*` family compiles to those primitives directly. A hand-rolled equivalent — whether it goes through LDS atomics, a tree-reduction with barriers, or an explicit `WaveReadLaneAt` ladder — replaces those 5 dedicated cycles with 32-64 ALU operations plus LDS / barrier traffic.

Concretely: a 32-lane atomic-loop reduction issues 32 LDS-atomic round trips serialised on the atomic unit (~32x the latency of a single atomic, plus the barrier round trip if the result is consumed cross-wave). A 32-lane tree-reduction with barriers costs `log₂ 32 = 5` rounds of LDS read-write plus 5 `GroupMemoryBarrierWithGroupSync` calls — each barrier round is a full thread-group synchronisation, typically 30-60 cycles of dead time on RDNA / Ada / Xe-HPG. A hand-rolled `WaveReadLaneAt` ladder is closer to the wave-intrinsic in cost (no LDS / barrier traffic) but obscures intent — the optimiser may not pattern-match it back to the dedicated primitive, leaving 5 ALU rounds where one wave-intrinsic call would have done.

The win is consistent across IHVs: replacing the manual reduction with the wave intrinsic recovers 5-10x throughput on the reduction itself and frees the LDS atomic unit / barrier synchroniser for productive work elsewhere. The pattern is most common in older HLSL written before SM 6.0 wave intrinsics were widely available. It also persists in code paths that fall back to atomics out of habit.

## Examples

### Bad

```hlsl
// Tree reduction in groupshared with log2(N) barriers.
groupshared float g_Reduce[32];

[numthreads(32, 1, 1)]
void cs_manual_sum(uint gi : SV_GroupIndex) {
    g_Reduce[gi] = SrcBuffer[gi];
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 16; stride > 0; stride >>= 1) {
        if (gi < stride) {
            g_Reduce[gi] += g_Reduce[gi + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) Out[0] = g_Reduce[0];
}
```

### Good

```hlsl
// One wave-intrinsic, no LDS, no barriers (within the wave).
[numthreads(32, 1, 1)]
void cs_wave_sum(uint gi : SV_GroupIndex) {
    float v = SrcBuffer[gi];
    float waveSum = WaveActiveSum(v);
    if (gi == 0) Out[0] = waveSum;
}
```

## Options

none

## Fix availability

**suggestion** — Replacing a manual reduction with a wave intrinsic changes the reduction scope (wave vs workgroup) and the surrounding LDS / barrier structure. The diagnostic identifies the manual-reduction pattern and proposes the corresponding `WaveActive*` call; the author confirms the scope.

## See also

- Related rule: [groupshared-atomic-replaceable-by-wave](groupshared-atomic-replaceable-by-wave.md) — atomic-counter variant
- Related rule: [wave-prefix-sum-vs-scan-with-atomics](wave-prefix-sum-vs-scan-with-atomics.md) — scan variant
- Related rule: [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md) — small-bin pre-reduce
- HLSL intrinsic reference: `WaveActiveSum`, `WaveActiveBitOr`, `WaveActiveCountBits`
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-wave-reduction-pattern.md)

*© 2026 NelCit, CC-BY-4.0.*
