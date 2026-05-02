---
id: numthreads-too-small
category: workgroup
severity: warn
applicability: none
since-version: v0.5.0
phase: 3
---

# numthreads-too-small

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

A compute or amplification shader whose `[numthreads(X, Y, Z)]` attribute produces a total thread count (X * Y * Z) strictly less than the minimum hardware wave size of 32. Values such as `[numthreads(4, 4, 1)]` (16 threads), `[numthreads(1, 1, 1)]` (1 thread), and `[numthreads(8, 1, 1)]` (8 threads) all fire the rule. The threshold is fixed at 32 regardless of the `target-wave-size` option from `numthreads-not-wave-aligned`, because 32 is the minimum wave size on all currently targeted hardware families (AMD RDNA/RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace). The rule does not fire when the total thread count is exactly 32 or any positive multiple of 32.

## Why it matters on a GPU

A thread group with fewer than 32 threads can never fill a single wave. On RDNA 3 with a 32-wide wave, a thread group of 16 threads is launched as one wave with 16 active lanes and 16 permanently masked-off lanes. Those 16 masked lanes are not recycled by the hardware — the wave occupies a full wave slot in the CU's wave scheduler for the entire duration of the kernel, including all memory latency hiding. This means that at best 50% of the execution resources in that wave slot are doing productive work. Across a full dispatch of many thread groups, effective VALU throughput is capped at 50% of the achievable rate for that occupancy level.

The impact on occupancy is compounding. GPU architectures sustain high performance through latency hiding: while one wave is stalled on a memory operation, the hardware switches to another wave in the same CU/SM and continues executing. The number of in-flight waves per CU/SM is constrained by the wave-slot budget (typically 8-16 waves per CU on RDNA 3, 32 warps per SM on Turing). A thread group of 16 threads occupies one wave slot for the group and also requires one groupshared allocation. An alternative group of 64 threads per group dispatched at one-quarter the group count achieves the same total work with four times as many active lanes per wave slot, allowing better latency hiding and better VALU utilisation per slot.

There is no automated fix for this rule because changing the thread count from 16 to 64 is not a mechanical transformation: it may require restructuring the shader logic (more threads per group means each thread may process fewer pixels or elements), updating the CPU `Dispatch` call, and potentially revisiting groupshared indexing. The diagnostic is purely informational — it flags configurations that are very unlikely to be intentional and that carry a significant occupancy cost on all current hardware.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/workgroup.hlsl, lines 17-20
// HIT(numthreads-too-small): 4 * 4 = 16, smaller than minimum wave size of 32.
[numthreads(4, 4, 1)]
void cs_too_small(uint3 dtid : SV_DispatchThreadID) {
    Output[dtid.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
```

### Good

```hlsl
// 8 * 8 = 64 — fills two 32-wide waves; good occupancy on all hardware.
[numthreads(8, 8, 1)]
void cs_clean(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}

// Minimum acceptable: exactly one full wave.
[numthreads(32, 1, 1)]
void cs_min_acceptable(uint3 dtid : SV_DispatchThreadID) {
    Output[dtid.xy] = float4(1.0, 0.0, 0.0, 1.0);
}
```

## Options

none

## Fix availability

**none** — Increasing the thread group size requires restructuring shader logic, updating CPU-side `Dispatch` parameters, and reviewing groupshared memory layout and indexing. No automated fix is offered. Add `// hlsl-clippy: allow(numthreads-too-small)` to suppress the diagnostic on a specific kernel where a small thread count is intentional (for example, a wave-intrinsic kernel that must run exactly 8 threads for algorithmic reasons).

## See also

- Related rule: [`numthreads-not-wave-aligned`](numthreads-not-wave-aligned.md) — thread group total not divisible by wave size
- Related rule: [`groupshared-too-large`](groupshared-too-large.md) — groupshared memory limits that reduce occupancy further
- HLSL reference: `[numthreads]` attribute in the DirectX HLSL Shader Model 6.x documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/numthreads-too-small.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
