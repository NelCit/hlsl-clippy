---
id: groupshared-first-read-without-barrier
category: workgroup
severity: error
applicability: none
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# groupshared-first-read-without-barrier

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A read of `gs[expr]` reachable from the kernel entry without any preceding `GroupMemoryBarrierWithGroupSync` (or equivalent group-syncing barrier) on at least one CFG path, when `expr` may resolve to a cell that *some other thread* may have written. This is distinct from [groupshared-uninitialized-read](groupshared-uninitialized-read.md), which fires when *no thread* has written the cell on any path; this rule fires when one or more threads *have* written the cell but the reader cannot rely on those writes being visible because no barrier has executed in between.

## Why it matters on a GPU

The HLSL / D3D12 memory model treats groupshared memory as unordered across lanes until a `GroupMemoryBarrier*` makes prior writes visible. Without that barrier, a lane reading `gs[some_other_lane_index]` is racing the lane that wrote that cell. The race is real on every modern GPU: AMD RDNA 2/3 issues LDS reads through the LDS read port without a wait on prior writes from sibling waves in the same workgroup; NVIDIA Ada's shared-memory unit similarly does not auto-fence cross-warp reads; Intel Xe-HPG SLM behaves identically. The hazard often hides behind a same-wave coincidence (within a single wave, lanes execute in lockstep on most IHVs, so a same-wave write-then-read happens to work), but as soon as a workgroup contains more than one wave, cross-wave reads see undefined values — sometimes the new write, sometimes the old.

The cross-lane race surfaces in two distinct ways. The first is wrong output: the kernel produces correct results during initial development on small workgroup sizes (one wave) and breaks when the workgroup grows past 32 (RDNA wave32 / NVIDIA / Xe-HPG) or 64 (RDNA wave64). The second is a vendor-divergent crash mode: one IHV happens to schedule the writer wave first, hiding the bug; another schedules the reader wave first, exposing it. Both patterns are notoriously hard to debug because LDS values are not visible to PIX / RenderDoc / NSight without inserting tap-out writes that themselves perturb the timing.

The fix is uniform: place a `GroupMemoryBarrierWithGroupSync()` between the producing writes and the cross-thread read. The barrier costs one full thread-group synchronisation but is unavoidable for cross-wave LDS communication. The rule is distinct from [groupshared-uninitialized-read](groupshared-uninitialized-read.md) because the suppression scope differs: uninitialised-read is a "no producer at all" bug; this rule is a "producer exists but is not synchronised" bug. Authors should be able to suppress one without the other.

## Examples

### Bad

```hlsl
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_race_no_barrier(uint gi : SV_GroupIndex) {
    g_Tile[gi] = SrcBuffer[gi];
    // No GroupMemoryBarrierWithGroupSync here. Lane gi reads cell (gi+1)%64
    // which another lane wrote — race across waves.
    Out[gi] = g_Tile[(gi + 1) % 64];
}
```

### Good

```hlsl
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_synced(uint gi : SV_GroupIndex) {
    g_Tile[gi] = SrcBuffer[gi];
    // All threads in the workgroup must reach this barrier before any
    // cross-thread LDS read may begin.
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64];
}
```

## Options

none

## Fix availability

**none** — Inserting a barrier in the right location requires understanding the producer–consumer relationship between threads. A naive auto-insertion at the read site is often wrong (the barrier needs to dominate every write the read may observe). The diagnostic identifies the racing read and the writes that supply the cell; the author chooses where the barrier belongs.

## See also

- Related rule: [groupshared-uninitialized-read](groupshared-uninitialized-read.md) — read with no writer at all
- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — write-side phrasing of the same hazard
- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — barrier hazards in divergent CF
- HLSL intrinsic reference: `GroupMemoryBarrierWithGroupSync`
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-first-read-without-barrier.md)

*© 2026 NelCit, CC-BY-4.0.*
