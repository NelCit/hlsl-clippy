---
id: groupshared-overwrite-before-barrier
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# groupshared-overwrite-before-barrier

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A write to a `groupshared` cell `gs[i]` followed by a second write to the same cell from the same thread on every CFG path, with no `GroupMemoryBarrier`, `GroupMemoryBarrierWithGroupSync`, `AllMemoryBarrier`, or equivalent intervening synchronisation that could have made the first write observable to other threads. The classic case is `gs[gi] = init_value; gs[gi] = computed_value;` in a straight-line block. The rule also fires when the second store is conditional but dominates the join (every path from the first write reaches the second).

## Why it matters on a GPU

GPU memory ordering for groupshared / LDS is barrier-relative: a write becomes visible to other lanes in the workgroup only after a `GroupMemoryBarrier*` is executed by both the writer and the reader. With no intervening barrier, a write that is shadowed by a later write from the same thread is unobservable to every other thread — the second write completely supersedes the first in the LDS bank, and no consumer wave ever sees the original value. On AMD RDNA 2/3 the LDS write coalescer may fold back-to-back same-address stores into a single transaction, but the wasted instruction issue still occupies the LDS write port. On NVIDIA Ada the shared-memory atomic / store unit serialises the two writes; on Intel Xe-HPG the SLM behaves similarly. In every case the first write costs a full LDS write cycle and produces zero observable effect.

The pattern is overwhelmingly a refactor leftover. A common origin is initialising LDS to a sentinel ("clear to zero") and then immediately overwriting with the real value in the same straight-line block — the sentinel was useful when there used to be a barrier between the two writes but became dead when the barrier was removed. Another origin is conditional initialisation that the optimiser cannot prove is dominated by the unconditional reassignment; the dead-store hazard then surfaces as wasted LDS bandwidth on every iteration of the surrounding loop. Removing the dead first write recovers one LDS cycle per wave per iteration and clarifies the producer–consumer relationship that the surrounding barrier (if any) is meant to guard.

The rule is distinct from [groupshared-dead-store](groupshared-dead-store.md), which catches stores with no later reads anywhere; this rule catches stores with later reads that are unreachable because a same-thread overwrite intervenes. The diagnostic identifies the shadowed write and points at the dominating reassignment so the author can decide whether to delete the first write or insert the missing barrier.

## Examples

### Bad

```hlsl
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_double_write(uint gi : SV_GroupIndex) {
    g_Tile[gi] = 0.0;                  // first write, never observable
    g_Tile[gi] = SrcBuffer[gi];        // overwrites before any barrier
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64];
}
```

### Good

```hlsl
// Drop the dead initialiser — the second write supplies the same cell
// before any other thread can observe LDS.
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_single_write(uint gi : SV_GroupIndex) {
    g_Tile[gi] = SrcBuffer[gi];
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64];
}
```

## Options

none

## Fix availability

**suggestion** — The first write may have been intentional (a sentinel that some other path observes via a barrier the rule cannot prove is missing). The diagnostic identifies the shadowed write and the overwriting store; the author decides whether to delete the dead store or insert a barrier.

## See also

- Related rule: [groupshared-dead-store](groupshared-dead-store.md) — write with no subsequent read anywhere
- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — missing barrier between write and cross-thread read
- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — barrier hazards that interact with this pattern
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-overwrite-before-barrier.md)

*© 2026 NelCit, CC-BY-4.0.*
