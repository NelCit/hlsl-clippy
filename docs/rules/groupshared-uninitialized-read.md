---
id: groupshared-uninitialized-read
category: control-flow
severity: error
applicability: none
since-version: v0.5.0
phase: 4
---

# groupshared-uninitialized-read

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Reads from a `groupshared` variable or array element before any thread in the group has executed a write to that location in the current dispatch, and where no `GroupMemoryBarrierWithGroupSync` between a covering write and the read has been established. The rule fires when the first use of a `groupshared` symbol in the shader body (within a `[numthreads]`-annotated function) is a load expression rather than a store, and when no unconditional write precedes the read either in the same thread's code path or guarded by a barrier that covers all threads. It does not fire when the read is preceded by an unconditional write to the same location by the same thread, or when the pattern matches the established initialise-barrier-read idiom (all threads write, barrier, all threads read).

## Why it matters on a GPU

Groupshared memory (LDS on AMD, shared memory on NVIDIA, SLM on Intel Xe) is allocated per thread group and retains its value only for the duration of a single group's lifetime. At the start of a group's execution, the contents of its LDS allocation are undefined — the hardware does not zero-initialise it. On most implementations, LDS will contain whatever a previous thread group wrote to those addresses before it finished, or potentially hardware-specific reset values. Either way, reading before writing produces a value that is not under programmer control and will vary across hardware, driver versions, and group scheduling order.

Beyond the undefined value, the pattern is a data race in the D3D12 and Vulkan execution models. Within a thread group, threads are not guaranteed to execute in lock-step unless they are within the same wave. Two threads that share an LDS location may read and write it in any order relative to each other unless a `GroupMemoryBarrierWithGroupSync` enforces the sequencing. A barrier guarantees that all threads have completed their writes before any thread proceeds past the barrier. Without the barrier, the outcome of any LDS load that depends on a write from a different thread is explicitly undefined. On AMD RDNA architectures, this can manifest as the reading thread observing the value written by a thread in a previous wave within the same group, the write from the current wave, or a stale L1 cache line. On NVIDIA Turing and Ada, shared-memory hazards can manifest as non-deterministic read values because the write and the read may hit different phases of the SM's L1 shared-memory pipeline.

The correct pattern for LDS use is: (1) every thread writes its contribution unconditionally, (2) `GroupMemoryBarrierWithGroupSync` ensures all writes complete, (3) every thread reads. In algorithms where only some threads write (e.g., populating a prefix-sum buffer for active threads only), a thread-group-wide init pass (e.g., `if (gi < N) lds[gi] = 0;`) followed by a barrier and then the conditional write-and-read is the correct structure. Reading without this discipline produces intermittent corruption that is nearly impossible to reproduce in a debug configuration because debug drivers often clear LDS on group creation.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/loop_invariant.hlsl, line 46-54
// HIT(groupshared-uninitialized-read): GShared[gi] read before any thread
// in the group has written it.
groupshared float GShared[64];

[numthreads(64, 1, 1)]
void cs_groupshared_uninit_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // No write to GShared before the read below. Race + UB.
    GroupMemoryBarrierWithGroupSync();
    // GShared[gi] was never written by this dispatch — undefined value.
    float v = GShared[gi];
    GShared[gi] = v + (float)dtid.x;
}

// From tests/fixtures/phase4/loop_invariant_extra.hlsl, line 116-125
// HIT(groupshared-uninitialized-read): GUninit[gi] is read before any thread
// has written to it in this dispatch — undefined value.
groupshared float4 GUninit[128];

[numthreads(128, 1, 1)]
void cs_uninit_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    GroupMemoryBarrierWithGroupSync();
    float4 v = GUninit[gi];   // read before write — UB
    GUninit[gi] = v + BlurTex.SampleLevel(Linear, (float2)dtid.xy * InvRes, 0);
    GroupMemoryBarrierWithGroupSync();
    BlurOut[dtid.xy] = GUninit[gi];
}
```

### Good

```hlsl
// Initialise every LDS slot unconditionally, barrier, then read.
groupshared float GShared[64];

[numthreads(64, 1, 1)]
void cs_groupshared_init_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // Each thread writes its own slot — covers the whole array.
    GShared[gi] = (float)dtid.x;
    // Barrier ensures all writes are visible to all threads.
    GroupMemoryBarrierWithGroupSync();
    // Now safe to read a neighbour's slot.
    uint neighbour = (gi + 1) & 63u;
    float v = GShared[neighbour];
    GShared[gi] += v;
}

// Explicit zero-init pass before conditional write.
groupshared float4 GUninit[128];

[numthreads(128, 1, 1)]
void cs_zero_init_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // Zero-initialise every slot before any conditional write.
    GUninit[gi] = float4(0, 0, 0, 0);
    GroupMemoryBarrierWithGroupSync();
    // Now write the real value for this thread.
    GUninit[gi] = BlurTex.SampleLevel(Linear, (float2)dtid.xy * InvRes, 0);
    GroupMemoryBarrierWithGroupSync();
    BlurOut[dtid.xy] = GUninit[gi];
}
```

## Options

none

## Fix availability

**none** — Inserting an initialisation write changes the memory values visible to all threads and may alter algorithm correctness if the code relies on a specific pre-existing LDS layout (unlikely but possible in hand-tuned shaders). Additionally, identifying the correct initialisation value and the safe insertion point requires full control-flow and data-flow analysis. The diagnostic identifies the unguarded read and the groupshared variable; the fix must be applied manually.

## See also

- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — cross-thread read without barrier between write and read
- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — barrier inside divergent CF (deadlock hazard)
- HLSL intrinsic reference: `GroupMemoryBarrierWithGroupSync` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-uninitialized-read.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
