---
id: wave-prefix-sum-vs-scan-with-atomics
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# wave-prefix-sum-vs-scan-with-atomics

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A hand-rolled prefix-sum (exclusive or inclusive scan) implemented as a multi-pass groupshared-plus-barrier sequence. Pattern shapes detected: (a) a Hillis–Steele up-sweep of the form `for (uint stride = 1; stride < N; stride <<= 1) { if (gi >= stride) g_Scan[gi] += g_Scan[gi - stride]; GroupMemoryBarrierWithGroupSync(); }`, (b) a Blelloch up-sweep / down-sweep with the equivalent barrier ladder, and (c) any scan implemented as a sequence of `InterlockedAdd` against a running counter where lanes consume monotone slot indices. All three patterns can be replaced by `WavePrefixSum` (within a wave) plus at most one barrier-and-broadcast step (across waves in a workgroup).

## Why it matters on a GPU

Prefix-sum scan is the second-most-common cross-lane primitive in compute shaders (after `WaveActiveSum`). Every modern GPU implements it as a dedicated cross-lane primitive: AMD RDNA 2/3 issues `WavePrefixSum` through DPP (Data-Parallel Primitives) in `log₂(wave_size)` cycles — 5 cycles on a wave32 RDNA 3, 6 on a wave64 RDNA 2; NVIDIA Ada Lovelace and Turing expose the equivalent through the warp-shfl prefix network, also 5 cycles per warp; Intel Xe-HPG's subgroup prefix-scan completes in `log₂(subgroup_size)` cycles on the cross-lane unit. The HLSL `WavePrefixSum` intrinsic compiles to those primitives directly.

A hand-rolled scan with groupshared and barriers replaces those 5 cycles with `log₂(N)` rounds of: LDS read, ALU add, LDS write, full thread-group barrier. On a 64-lane workgroup that is 6 rounds; each barrier costs ~30-60 cycles of dead time on RDNA / Ada / Xe-HPG, so the barrier ladder alone is ~200-360 cycles of pure synchronisation latency, plus the LDS round-trip ALU work. The wave-intrinsic version completes the within-wave scan in 5 cycles and needs at most one cross-wave broadcast step (groupshared write of the wave's total + one barrier + LDS read of the prefix base) for a workgroup-scoped scan. End-to-end the wave intrinsic recovers ~10-30x throughput depending on wave count per workgroup.

The atomic-monotone-slot variant is even worse: a per-lane `InterlockedAdd(g_Counter, 1)` to claim a slot serialises 32-64 lanes on the LDS atomic unit, taking 32-64 atomic round trips for what `WavePrefixSum(1) + WaveActiveSum(1) + one wave-leader atomic` accomplishes in three steps. The fix template is uniform: `WavePrefixSum` for the per-lane prefix, `WaveActiveSum` for the wave total, one atomic from the wave leader to publish the wave's base offset, then add the wave base to each lane's prefix.

## Examples

### Bad

```hlsl
// Hillis-Steele scan: 6 rounds × (LDS rw + barrier) for a 64-lane workgroup.
groupshared uint g_Scan[64];

[numthreads(64, 1, 1)]
void cs_manual_scan(uint gi : SV_GroupIndex) {
    g_Scan[gi] = SrcBuffer[gi];
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 1; stride < 64; stride <<= 1) {
        uint v = (gi >= stride) ? g_Scan[gi - stride] : 0;
        GroupMemoryBarrierWithGroupSync();
        g_Scan[gi] += v;
        GroupMemoryBarrierWithGroupSync();
    }
    Out[gi] = g_Scan[gi];
}
```

### Good

```hlsl
// Wave-intrinsic scan: one prefix call within the wave, one barrier and one
// atomic publish to stitch waves together.
groupshared uint g_WaveBase[2];   // 2 waves of 32 each per 64-lane group

[numthreads(64, 1, 1)]
void cs_wave_scan(uint gi : SV_GroupIndex) {
    uint v = SrcBuffer[gi];
    uint inWave = WavePrefixSum(v);
    uint waveSum = WaveActiveSum(v);

    // Wave leader publishes its total to a per-wave slot.
    uint waveId = gi / WaveGetLaneCount();
    if (WaveIsFirstLane()) {
        g_WaveBase[waveId] = waveSum;
    }
    GroupMemoryBarrierWithGroupSync();

    // Each lane adds the prefix-sum of all earlier waves' totals.
    uint base = 0;
    for (uint w = 0; w < waveId; ++w) base += g_WaveBase[w];

    Out[gi] = base + inWave;
}
```

## Options

none

## Fix availability

**suggestion** — The transformation requires choosing between within-wave only (one `WavePrefixSum` call) and within-workgroup (the wave-intrinsic + cross-wave stitching shown above). The diagnostic identifies the manual-scan pattern and proposes the appropriate replacement based on the original scan's scope.

## See also

- Related rule: [groupshared-atomic-replaceable-by-wave](groupshared-atomic-replaceable-by-wave.md) — single-counter atomic version
- Related rule: [manual-wave-reduction-pattern](manual-wave-reduction-pattern.md) — reduction sibling rule
- Related rule: [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md) — small-bin pre-reduce
- HLSL intrinsic reference: `WavePrefixSum`, `WaveActiveSum`, `WaveGetLaneCount`
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/wave-prefix-sum-vs-scan-with-atomics.md)

*© 2026 NelCit, CC-BY-4.0.*
