---
id: groupshared-write-then-no-barrier-read
category: workgroup
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# groupshared-write-then-no-barrier-read

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Compute shader code paths where a thread writes a `groupshared` location and a different thread (or the same thread on a subsequent iteration of a loop with cross-iteration dependence) reads from the same array region without a `GroupMemoryBarrierWithGroupSync` or `AllMemoryBarrierWithGroupSync` between the write and the read. The rule analyses index expressions to determine when reads can target a slot another thread has written: writes indexed by `SV_GroupIndex` followed by reads indexed by anything other than the same `SV_GroupIndex` (a neighbour offset, a constant, a transposed coordinate) are the canonical hits.

## Why it matters on a GPU

The D3D12 compute model guarantees that within a thread group, all threads share LDS / groupshared memory, but it does NOT guarantee any ordering between threads' memory operations unless an explicit barrier is issued. On AMD RDNA 2/3, threads in a wave run in lock-step, but threads in different waves of the same group are scheduled independently and can be hundreds of cycles apart at any given instruction; without a barrier, a read from another wave's slot may return the previous frame's value, the LDS-uninitialised pattern (zero on AMD, undefined on NVIDIA), or a torn write halfway through a non-atomic store. On NVIDIA Turing / Ada, the same applies across warps in a thread block: the SM scheduler issues warps in arbitrary order and the L1/SHM coherence boundary is the explicit barrier instruction. On Intel Xe-HPG, the EU thread scheduler likewise serialises across barrier points only.

The bug class is insidious because the wave size happens to align with the access pattern in many cases. A reduction algorithm that writes `Sum[gi] = value` and then reads `Sum[gi ^ 1]` works correctly on hardware where the wave size is at least the group size — both writer and reader are in the same wave and lock-step execution provides accidental ordering — but breaks on hardware with a smaller wave size where the writer and reader land in different waves. RDNA 2/3 supports both wave32 and wave64 modes selected by `[WaveSize(32)]` or compiler heuristics; a shader that "works" on one wave size can deadlock or produce wrong output when the driver chooses the other. The bug also manifests after a driver upgrade that changes wave-size selection.

The fix is straightforward: insert `GroupMemoryBarrierWithGroupSync()` between the write phase and the read phase. The barrier is a single instruction that costs roughly 8-20 cycles depending on group size — modest compared to the cost of any reduction or transpose pass — and provides the cache-flush + execution-fence semantics required for the cross-thread visibility. For algorithms that perform many alternating write/read passes (like log-step reductions or radix sort phases), each phase boundary needs its own barrier; omitting any of them reintroduces the race.

## Examples

### Bad

```hlsl
groupshared float g_Sum[64];

[numthreads(64, 1, 1)]
void cs_reduce(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g_Sum[gi] = Input[dtid.x];
    // No barrier — readers in another wave (or even the same wave under a
    // future scheduler change) may see uninitialised LDS values.
    if (gi < 32) {
        g_Sum[gi] += g_Sum[gi + 32];
    }
}
```

### Good

```hlsl
groupshared float g_Sum[64];

[numthreads(64, 1, 1)]
void cs_reduce_safe(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g_Sum[gi] = Input[dtid.x];
    // Make the writes globally visible before any thread reads a neighbour slot.
    GroupMemoryBarrierWithGroupSync();
    if (gi < 32) {
        g_Sum[gi] += g_Sum[gi + 32];
    }
    // Each subsequent reduction step needs its own barrier.
    GroupMemoryBarrierWithGroupSync();
    if (gi < 16) g_Sum[gi] += g_Sum[gi + 16];
    GroupMemoryBarrierWithGroupSync();
    if (gi <  8) g_Sum[gi] += g_Sum[gi +  8];
    // ... and so on.
}
```

## Options

none

## Fix availability

**none** — Inserting a barrier changes execution timing and program structure. Although the canonical fix is a single barrier insertion at the write/read boundary, identifying the correct insertion point requires understanding the producer/consumer schedule of the algorithm. The diagnostic flags the write site, the read site, and the absence of an intervening barrier on the dominator path between them.

## See also

- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — barriers placed inside divergent control flow
- Related rule: [groupshared-uninitialized-read](groupshared-uninitialized-read.md) — reading a groupshared slot before any thread has written
- Related rule: [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) — bank conflict patterns
- HLSL intrinsic reference: `GroupMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-write-then-no-barrier-read.md)

*© 2026 NelCit, CC-BY-4.0.*
