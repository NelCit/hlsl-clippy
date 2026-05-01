---
id: reordercoherent-uav-missing-barrier
category: ser
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# reordercoherent-uav-missing-barrier

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A UAV qualified `[reordercoherent]` whose write site reaches a read site across a `MaybeReorderThread` reordering point on at least one CFG path, without an intervening barrier or memory-fence. The SER specification's `[reordercoherent]` qualifier promises the runtime that the application has explicit synchronisation around the reorder; missing that synchronisation is undefined behaviour because the reorder shuffles lanes between the write and the read in a way the cache hierarchy can no longer correlate. The Phase 4 analysis crosses CFG paths through the reorder point and verifies the barrier presence.

## Why it matters on a GPU

`[reordercoherent]` is the SER-specific cousin of `globallycoherent`: it tells the runtime that the UAV's coherence has to survive a `MaybeReorderThread` event. The reorder physically rearranges lanes within the wave (and on some implementations across waves), so a write from "old lane 5" and a read from "new lane 5" are unrelated unless the runtime forces the L1 cache to flush the old write before the reorder. The `[reordercoherent]` qualifier is the contract that the application has placed an explicit barrier or fence around the reorder point; without one, the read sees stale data.

On NVIDIA Ada Lovelace, the missing barrier produces silently-wrong results — the L1 cache caches the write per-SM and the post-reorder lane reads its old SM's stale entry. AMD RDNA 4's SER implementation and the Vulkan equivalent exhibit the same pattern. There is no driver diagnostic; the failure mode is wrong pixels or NaN propagation. Catching it at lint time is the only way to surface the bug before it reaches production.

The fix is to add `DeviceMemoryBarrier()` (or the appropriate fence intrinsic) before or after the reorder point, depending on whether the write site precedes or follows the reorder. The diagnostic emits the candidate barrier placement.

## Examples

### Bad

```hlsl
// Write before reorder; read after; no barrier — UB.
[reordercoherent] RWStructuredBuffer<float> g_Cache : register(u0);

[shader("raygeneration")]
void RayGen() {
    uint laneId = WaveGetLaneIndex();
    g_Cache[laneId] = ComputeSomething();          // write
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, /*...*/);
    dx::MaybeReorderThread(hit);                   // reorder
    float v = g_Cache[laneId];                     // read post-reorder; stale
    /* ... */
}
```

### Good

```hlsl
[reordercoherent] RWStructuredBuffer<float> g_Cache : register(u0);

[shader("raygeneration")]
void RayGen() {
    uint laneId = WaveGetLaneIndex();
    g_Cache[laneId] = ComputeSomething();
    DeviceMemoryBarrier();                         // explicit fence
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, /*...*/);
    dx::MaybeReorderThread(hit);
    float v = g_Cache[laneId];
    /* ... */
}
```

## Options

none

## Fix availability

**none** — The right barrier placement and kind depends on the surrounding control flow. The diagnostic names the offending pair and emits a candidate barrier placement as a comment.

## See also

- Related rule: [rwbuffer-store-without-globallycoherent](rwbuffer-store-without-globallycoherent.md) — companion coherence rule for non-SER UAVs
- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — companion barrier-correctness rule
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/reordercoherent-uav-missing-barrier.md)

*© 2026 NelCit, CC-BY-4.0.*
