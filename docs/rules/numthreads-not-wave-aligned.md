---
id: numthreads-not-wave-aligned
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# numthreads-not-wave-aligned

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

A compute or amplification shader whose `[numthreads(X, Y, Z)]` attribute produces a total thread count (X * Y * Z) that is not a multiple of the configured wave size. The default `target-wave-size` is 32 (matching NVIDIA Turing/Ada and AMD RDNA/RDNA 2/RDNA 3 in their default 32-wide mode). A total that is not divisible by 32 means the final wave of every thread group is launched with some lanes masked off — hardware dead weight. The rule fires on `[numthreads(7, 7, 1)]` (49 threads, not a multiple of 32 or 64), `[numthreads(5, 13, 1)]` (65 threads), and similar configurations. It does not fire when the total is an exact multiple of the configured wave size.

## Why it matters on a GPU

The wave (or warp, in NVIDIA terminology) is the atomic unit of GPU execution. A wave is a group of lanes that share a single instruction stream: all lanes issue the same instruction on the same clock cycle, with divergent lanes masked off. On NVIDIA Turing and Ada Lovelace, the hardware wave size is 32 (a warp of 32 threads). On AMD RDNA and RDNA 2/3, the hardware supports both 32-wide and 64-wide waves; the driver defaults to 32-wide for SM6.x compute unless the shader explicitly requests 64-wide via `[WaveSize(64)]`. On Intel Xe-HPG, the hardware SIMD width is 8, 16, or 32 lanes; the compiler chooses based on register pressure.

When a thread group has 49 threads (7x7x1) and the hardware wave size is 32, the driver schedules two waves: the first covers threads 0-31 (32 lanes, all active), the second covers threads 32-48 (17 lanes active, 15 lanes masked off). The 15 masked lanes still consume ALU time in the second wave — they issue with a mask applied, but the execution unit is still serialised on them. This wastes 15/32 = 47% of the ALU throughput for the second wave. Across a full dispatch of many thread groups, this translates to a direct occupancy reduction proportional to the mask waste fraction.

Beyond raw ALU waste, masked-off lanes affect wave-level intrinsics such as `WaveReadLaneFirst`, `WaveActiveSum`, and `WavePrefixSum`. These intrinsics operate over all active lanes in the wave. With 15 masked-off lanes, the wavefront still has its full hardware footprint in the wave scheduler, consuming a wave slot in the CU/SM occupancy count — a full 32-lane wave slot for what is effectively a 17-lane workload. This is particularly costly on AMDs RDNA architecture, where the GDS (global data store) and groupshared memory are allocated per wave slot, not per active lane. A wave with 15 dead lanes wastes the same groupshared allocation as a fully-active wave.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/workgroup.hlsl, lines 9-15
// HIT(numthreads-not-wave-aligned): 7 * 7 = 49, not a multiple of 32 (NV) or 64 (AMD).
[numthreads(7, 7, 1)]
void cs_misaligned(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}

// From tests/fixtures/phase3/workgroup_extra.hlsl, line 66
// HIT(numthreads-not-wave-aligned): 5 * 13 = 65, not a multiple of 32 or 64.
[numthreads(5, 13, 1)]
void cs_misaligned_and_conflict(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // ...
}
```

### Good

```hlsl
// 8 * 8 = 64 — wave-aligned for AMD (64-wide), two full waves for NV (32-wide).
// SHOULD-NOT-HIT(numthreads-not-wave-aligned)
[numthreads(8, 8, 1)]
void cs_clean(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}

// 64 * 1 = 64 — also valid; two 32-wide waves or one 64-wide wave.
[numthreads(64, 1, 1)]
void cs_linear(uint3 dtid : SV_DispatchThreadID) {
    Output[dtid.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
```

## Options

- `target-wave-size` (integer, default: `32`) — the wave size to align to. Set to `64` for AMD GCN or AMD-targeted projects that use 64-wide waves exclusively. The rule fires when `(X * Y * Z) % target-wave-size != 0`. To configure per-project:

```toml
[rules.numthreads-not-wave-aligned]
target-wave-size = 64
```

## Fix availability

**suggestion** — The rule proposes the nearest wave-aligned thread count that preserves the work coverage: rounding up X or Y to the next multiple of `target-wave-size`, or flattening to a 1D layout with a wave-aligned total. Because changing the thread group dimensions may require updating the `DispatchThreadID` interpretation in the shader body and the `Dispatch(X, Y, Z)` call counts on the CPU side, `hlsl-clippy fix` shows the candidate `[numthreads]` change but does not apply it automatically.

## See also

- Related rule: [`numthreads-too-small`](numthreads-too-small.md) — thread group total smaller than the minimum wave size
- Related rule: [`groupshared-too-large`](groupshared-too-large.md) — groupshared memory exceeding occupancy thresholds
- HLSL reference: `[numthreads]` attribute, `WaveSize` in the DirectX HLSL Shader Model 6.x documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/numthreads-not-wave-aligned.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
