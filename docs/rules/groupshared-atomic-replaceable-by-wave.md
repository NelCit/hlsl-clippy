---
id: groupshared-atomic-replaceable-by-wave
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# groupshared-atomic-replaceable-by-wave

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

`InterlockedAdd(gs[k], expr)`, `InterlockedOr(gs[k], mask)`, `InterlockedAnd(gs[k], mask)`, `InterlockedXor(gs[k], mask)`, `InterlockedMin(gs[k], val)`, or `InterlockedMax(gs[k], val)` against a *single* groupshared cell — typically a counter at index 0 — where every active lane in the wave contributes a value derivable via the corresponding `WaveActive*` reduction. The rule fires when one wave-reduce + a single representative-lane atomic would replace 32 (RDNA wave32 / NVIDIA / Xe-HPG) or 64 (RDNA wave64) per-lane LDS atomics with one. Distinct from [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md), which targets a small fixed set of bins; this rule targets accumulation into one cell.

## Why it matters on a GPU

LDS atomics on every modern GPU serialise on the cell address. When a wave of 32 lanes (or 64 on RDNA wave64) hits the same `InterlockedAdd(gs[0], val)` site, the LDS atomic unit processes them sequentially: 32 round trips through the atomic ALU on AMD RDNA 2/3, 32 through NVIDIA Ada's shared-memory atomic unit, similar on Intel Xe-HPG. Even with hardware coalescing of monotonic-add operations on some IHVs, the worst case is 32x the single-atomic latency, and the atomic unit is single-issue per cycle so it stalls every other LDS access in the same wave for the duration. A 64-lane wave on RDNA wave64 doubles the cost.

The wave-reduce idiom collapses this to one round trip. `WaveActiveSum(val)` runs in a handful of cycles using the wave's cross-lane DPP / shfl hardware (single-issue, one or two cycles to reduce 32 lanes on RDNA 3 / Ada / Xe-HPG); only one lane then performs the LDS atomic, contributing the entire wave's sum. The end result on the LDS counter is identical, but the LDS atomic unit is freed for other waves, the per-lane atomic latency is gone, and the LDS bandwidth occupied by 32 separate atomic transactions collapses to one. For OR / AND / XOR / MIN / MAX the equivalent reductions (`WaveActiveBitOr`, `WaveActiveBitAnd`, `WaveActiveBitXor`, `WaveActiveMin`, `WaveActiveMax`) supply the wave-folded operand.

The pattern matters most in stream-compaction kernels (count survivors), histogram bins (when the number of bins is small and one wave covers a single bin), and visibility-buffer accumulators. On an RDNA 3 wave32 with one wave per workgroup, dropping to one atomic recovers ~30 atomic-unit cycles per workgroup; with 8 waves per workgroup that's ~240 cycles per workgroup of pure serialised atomic latency saved. The cross-IHV win is universal because every modern GPU implements LDS atomics as a serialising primitive.

## Examples

### Bad

```hlsl
groupshared uint g_Counter;

[numthreads(64, 1, 1)]
void cs_count_survivors(uint gi : SV_GroupIndex, uint3 dtid : SV_DispatchThreadID) {
    if (gi == 0) g_Counter = 0;
    GroupMemoryBarrierWithGroupSync();

    bool alive = SrcBuffer[dtid.x] > 0;
    if (alive) {
        // 64 lanes serialise on g_Counter — worst case 64 LDS-atomic round trips.
        InterlockedAdd(g_Counter, 1);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) Out[0] = g_Counter;
}
```

### Good

```hlsl
groupshared uint g_Counter;

[numthreads(64, 1, 1)]
void cs_count_survivors_wave(uint gi : SV_GroupIndex, uint3 dtid : SV_DispatchThreadID) {
    if (gi == 0) g_Counter = 0;
    GroupMemoryBarrierWithGroupSync();

    bool alive = SrcBuffer[dtid.x] > 0;
    // Wave-fold the contribution; one lane performs the atomic for the whole wave.
    uint wave_sum = WaveActiveCountBits(alive);
    if (WaveIsFirstLane()) {
        InterlockedAdd(g_Counter, wave_sum);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) Out[0] = g_Counter;
}
```

## Options

none

## Fix availability

**suggestion** — The transformation requires choosing the correct `WaveActive*` reduction (Sum / BitOr / BitAnd / BitXor / Min / Max / CountBits) for the atomic op being replaced and gating the atomic on `WaveIsFirstLane()`. The diagnostic identifies the atomic site and the per-lane operand and proposes the corresponding wave reduction.

## See also

- Related rule: [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md) — small-bin variant of the same pattern
- Related rule: [manual-wave-reduction-pattern](manual-wave-reduction-pattern.md) — manually-coded reductions that should use wave intrinsics
- Related rule: [wave-prefix-sum-vs-scan-with-atomics](wave-prefix-sum-vs-scan-with-atomics.md) — scan replacement
- HLSL intrinsic reference: `WaveActiveSum`, `WaveActiveCountBits`, `WaveIsFirstLane`
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-atomic-replaceable-by-wave.md)

*© 2026 NelCit, CC-BY-4.0.*
