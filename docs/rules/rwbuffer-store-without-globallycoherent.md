---
id: rwbuffer-store-without-globallycoherent
category: bindings
severity: error
applicability: none
since-version: v0.5.0
phase: 4
---

# rwbuffer-store-without-globallycoherent

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A write to a `RWBuffer`, `RWStructuredBuffer`, `RWByteAddressBuffer`, or `RWTexture*` UAV `U` followed — within the *same* dispatch — by a read of `U` from a different wave / thread group with no `DeviceMemoryBarrier` / `AllMemoryBarrier` and no `globallycoherent` qualifier on the UAV declaration. The rule fires when the CFG analysis can prove the write and read are reachable in the same dispatch and the cross-wave consumer path exists. This is a D3D12-flavoured rule: Vulkan and Metal use different memory-model surfaces (subgroup uniform / non-coherent / Metal2 buffer atomics) for the same hazard, so the rule's surface here is specifically the HLSL `globallycoherent` qualifier.

## Why it matters on a GPU

D3D12 UAV memory traffic on every modern GPU is cached at the per-CU / per-SM L1 level by default. AMD RDNA 2/3 routes UAV writes through the per-CU L1 / VMEM cache; only when the cache line is evicted (LRU pressure) or explicitly flushed does the write reach L2 / HBM where another CU can see it. NVIDIA Ada caches UAV writes in the per-SM L1; the L2 is shared across SMs but the L1 is not, so a read on a different SM sees stale L1-cached data until a flush. Intel Xe-HPG behaves similarly with the per-Xe-core L1. The HLSL `globallycoherent` qualifier on a UAV declaration tells the compiler to bypass the per-unit L1 (or to flush after each write, depending on IHV), routing all access through L2 / shared coherent memory. Without that qualifier the cross-wave read may see arbitrary stale data — the bug is observable only on dispatches large enough to spill across multiple compute units, which makes it a classic "works in dev, fails in prod" hazard.

The hazard is hardest to spot in producer-consumer compute pipelines that do not insert an explicit barrier between phases (because the algorithm is structured as a single dispatch with internal cross-wave reads — typical of bitonic sort, prefix-sum scan, or work-stealing dispatchers). On D3D12, the application-level fix is either (a) split the dispatch in two with a UAV barrier in between, (b) add `globallycoherent` to the UAV declaration, or (c) restructure the algorithm so cross-wave reads only happen across dispatch boundaries. Each has cost: option (a) loses the dispatch-level fusion benefit; option (b) costs L1 hit-rate on every UAV access; option (c) requires algorithmic redesign. The rule does not prefer one over the others — it surfaces the hazard so the author can choose.

On Vulkan, the equivalent surface is `coherent` SPIR-V buffer storage qualifiers and `vkCmdPipelineBarrier` between dispatch phases; on Metal, buffer atomics and Metal 2 `nonuniform` access patterns provide the same control. The HLSL → SPIR-V cross-compiler maps `globallycoherent` to SPIR-V `Coherent` decorations, so HLSL authors targeting Vulkan get the same fix; Metal authors typically write the equivalent qualifiers in MSL directly. This rule's diagnostic phrasing is HLSL/D3D12-first because that is the source-language surface.

## Examples

### Bad

```hlsl
// Single-dispatch producer/consumer with no globallycoherent and no barrier.
RWStructuredBuffer<float> g_Scratch : register(u0);

[numthreads(64, 1, 1)]
void cs_pipeline(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // Producer phase: write per-thread result.
    g_Scratch[dtid.x] = compute_phase_one(dtid.x);

    // Cross-wave consumer phase. Lane reads a cell another wave may have written
    // — but the per-CU L1 may still hold the old value. Stale data on RDNA 2/3
    // and Ada when dispatched across multiple compute units.
    float neighbor = g_Scratch[(dtid.x + 1024) % g_BufferSize];
    g_Scratch[dtid.x] = combine(g_Scratch[dtid.x], neighbor);
}
```

### Good

```hlsl
// Option A — mark the UAV globallycoherent so writes bypass the per-CU L1.
globallycoherent RWStructuredBuffer<float> g_Scratch : register(u0);

[numthreads(64, 1, 1)]
void cs_pipeline_coherent(uint3 dtid : SV_DispatchThreadID) {
    g_Scratch[dtid.x] = compute_phase_one(dtid.x);
    // ... cross-wave reads now see authoritative data
}

// Option B — split into two dispatches with an application-side UAV barrier
// between them (preferred when the L1 hit-rate cost of globallycoherent
// outweighs the dispatch-fusion benefit).
RWStructuredBuffer<float> g_Scratch : register(u0);

[numthreads(64, 1, 1)]
void cs_phase_one(uint3 dtid : SV_DispatchThreadID) {
    g_Scratch[dtid.x] = compute_phase_one(dtid.x);
}

[numthreads(64, 1, 1)]
void cs_phase_two(uint3 dtid : SV_DispatchThreadID) {
    float neighbor = g_Scratch[(dtid.x + 1024) % g_BufferSize];
    g_Scratch[dtid.x] = combine(g_Scratch[dtid.x], neighbor);
}
```

## Options

none

## Fix availability

**none** — Three valid fixes exist (qualifier change, dispatch split, algorithmic restructure) with very different runtime costs. The diagnostic identifies the producing write, the consuming read, and the missing qualifier / barrier; the author chooses the resolution.

## See also

- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — same hazard family at LDS scope
- Related rule: [groupshared-first-read-without-barrier](groupshared-first-read-without-barrier.md) — barrier-omission at LDS scope
- Related rule: [uav-srv-implicit-transition-assumed](uav-srv-implicit-transition-assumed.md) — application-side barrier audit hint
- HLSL reference: `globallycoherent` qualifier in the DirectX HLSL specification
- Vulkan equivalent: SPIR-V `Coherent` decoration, `vkCmdPipelineBarrier`
- Metal equivalent: buffer atomics and MSL access qualifiers
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/rwbuffer-store-without-globallycoherent.md)

*© 2026 NelCit, CC-BY-4.0.*
