---
id: barrier-in-divergent-cf
category: control-flow
severity: error
applicability: none
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# barrier-in-divergent-cf

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `GroupMemoryBarrierWithGroupSync`, `DeviceMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync`, and the variants without `WithGroupSync` (`GroupMemoryBarrier`, `DeviceMemoryBarrier`, `AllMemoryBarrier`) when they appear inside a branch whose condition depends on a non-uniform value. Non-uniform in this context means any condition that is not provably identical for all threads in the thread group simultaneously — including conditions derived from `SV_DispatchThreadID`, `SV_GroupIndex`, per-pixel varying data, or any value loaded from a non-constant buffer with a thread-varying index. The rule fires on any `if`, `else if`, `else`, `for`, `while`, or `switch` that contains a synchronising barrier intrinsic and whose predicate is not demonstrably thread-group-uniform.

## Why it matters on a GPU

GPU compute shaders run as independent thread groups. Within a thread group, threads share a pool of groupshared (LDS) memory and are expected to coordinate via barriers. A `GroupMemoryBarrierWithGroupSync` tells the hardware: stall every thread in this group until all of them reach this instruction. The GPU implements this by counting how many threads have checked in; only when the count reaches the group size does execution resume. If some threads never reach the barrier — because they took a divergent branch — the counter never reaches the group size. The result is a GPU hang: the threads that reached the barrier wait indefinitely. On AMD GCN and RDNA architectures, this stalls the entire compute unit because the scheduler cannot retire the wavefront. On NVIDIA architectures, the warp-level implementation deadlocks similarly. Neither the DX12 runtime nor the driver can detect this class of hang at API level; it manifests as a device-removed error or a TDR reset in production.

Even the non-syncing barrier variants (`GroupMemoryBarrier` without `WithGroupSync`) exist to flush caches for memory ordering purposes. Placing them in divergent control flow does not cause a hang, but it produces incorrect ordering guarantees: the threads that skip the barrier may observe stale LDS values written by threads that did execute the barrier, because the cache flush was not applied uniformly. This is a data race on groupshared memory — undefined behaviour in the D3D12 execution model — and is harder to debug than the hang case because it produces intermittently wrong output rather than a reliable crash.

The correct pattern is to either move the barrier to a location in the shader that is always reached by all threads (uniform control flow), or restructure the algorithm so the groupshared communication and synchronisation happen before any thread-varying branch. A common source of this bug is early-exit guards of the form `if (dtid.x >= count) return;` placed before a barrier that should have preceded the guard. The fix is to place the barrier before the early exit or to ensure the early-exit threads remain active through the barrier point.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 25-32
// HIT(barrier-in-divergent-cf): UB — only some lanes hit the barrier.
[numthreads(64, 1, 1)]
void cs_barrier_in_branch(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x < 32) {
        // Only the first 32 threads reach this barrier. The remaining 32
        // never arrive — the group hangs indefinitely.
        GroupMemoryBarrierWithGroupSync();
        Sum = (float)dtid.x;
    }
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 110-120
// HIT(barrier-in-divergent-cf): GroupMemoryBarrier inside a divergent
// branch — only some lanes reach this barrier, causing GPU hang / UB.
[numthreads(64, 1, 1)]
void cs_nested_divergent_barrier(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    LDSTile[gi] = (float)dtid.x;
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    if (noise > Threshold) {
        // 'noise' is data-dependent: different threads take different paths.
        GroupMemoryBarrierWithGroupSync();
        LDSTile[gi] *= 2.0;
    }
}
```

### Good

```hlsl
// Move the barrier to uniform control flow — all threads participate.
[numthreads(64, 1, 1)]
void cs_barrier_uniform(uint3 dtid : SV_DispatchThreadID) {
    // All threads write their contribution unconditionally.
    Sum = (dtid.x < 32) ? (float)dtid.x : 0.0;
    // Barrier in uniform control flow — all 64 threads reach it.
    GroupMemoryBarrierWithGroupSync();
    // Now read the fully-initialised shared value.
    float result = Sum;
    // Thread-varying work happens after the barrier.
    if (dtid.x < 32) {
        // ... process result
    }
}

// For the noise-driven case: restructure to separate the LDS phase from the
// thread-varying phase.
[numthreads(64, 1, 1)]
void cs_nested_barrier_fixed(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    LDSTile[gi] = (float)dtid.x;
    // Barrier while all threads are still active.
    GroupMemoryBarrierWithGroupSync();
    // Now each thread may diverge independently.
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    if (noise > Threshold) {
        LDSTile[gi] *= 2.0;
    }
}
```

## Options

none

## Fix availability

**none** — Restructuring control flow around a barrier changes program semantics and requires understanding the algorithm's producer/consumer relationship between threads. No automated fix is offered. The diagnostic points at the barrier call inside the divergent branch and identifies the non-uniform predicate expression.

## See also

- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — the same divergent-CF family for pixel shader derivatives
- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — wave operations in divergent control flow
- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — missing barrier between write and cross-thread read
- Related rule: [groupshared-uninitialized-read](groupshared-uninitialized-read.md) — groupshared read before any thread has written
- HLSL intrinsic reference: `GroupMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/barrier-in-divergent-cf.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
