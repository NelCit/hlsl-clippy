---
id: groupshared-dead-store
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# groupshared-dead-store

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A write to a `groupshared` cell where, on every CFG path from the store to workgroup exit, no read of that cell is reached — neither by the writing thread nor by any other thread (the rule conservatively assumes any thread can read any cell unless the index is provably thread-local). The simplest case is a write to `gs[gi] = expr;` followed by no further reads of `gs` anywhere in the kernel body. Compound cases include writes whose only downstream read is itself dead (transitive dead stores) and writes overwritten on every subsequent path before any barrier (caught more specifically by [groupshared-overwrite-before-barrier](groupshared-overwrite-before-barrier.md)).

## Why it matters on a GPU

LDS bandwidth and LDS occupancy are both finite and tightly budgeted. On AMD RDNA 2/3, each compute unit ships ~64 KB of LDS shared across all in-flight wavefronts; on NVIDIA Ada Lovelace, each SM provides 100 KB of unified L1/shared with the shared partition tunable per kernel; on Intel Xe-HPG, the SLM allocation per Xe-core is similar in scale. Every dead store still consumes an LDS write port for one cycle, still occupies a slot in the LDS write coalescer, and (more importantly) keeps the corresponding cell live for occupancy-budgeting purposes — the compiler cannot reclaim an LDS region that the source code still references. A kernel declared with a 16 KB groupshared array will be budgeted for 16 KB of occupancy footprint regardless of whether the writes are observed, capping wave concurrency on the CU/SM.

Dead stores additionally pollute the producer–consumer reasoning the rest of the analysis pass needs. A barrier inserted "just in case" because the author saw an unrelated dead store in the same shader is wasted work — the barrier still costs full thread-group synchronisation overhead. Removing dead stores both lowers LDS pressure and clarifies the actual cross-thread data flow, which often exposes barrier redundancy as a follow-up. The pattern is most common in shaders that have been refactored repeatedly: a temporary shared intermediate from one algorithm is left in place after the algorithm changes to use registers or wave intrinsics instead.

The rule shares the def-use scan that the locked `unused-cbuffer-field` rule needs (per ADR 0011). The analysis is conservative: any indexed read whose index cannot be statically resolved is treated as a possible read of every cell, so the rule fires only when no surviving read of the array can reach the store. False negatives are preferred over false positives; the cost of missing a dead store is one wasted LDS write, while the cost of a false positive is a regression in a load-bearing barrier.

## Examples

### Bad

```hlsl
// g_Tile is written but never read anywhere in the kernel body.
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_dead_lds(uint gi : SV_GroupIndex) {
    g_Tile[gi] = (float)gi;             // dead store: no subsequent read
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = SrcBuffer[gi] * 2.0;      // does not touch g_Tile
}
```

### Good

```hlsl
// Either drop the LDS array entirely if no consumer needs it,
// or wire up the consumer that justifies the store.
[numthreads(64, 1, 1)]
void cs_no_lds(uint gi : SV_GroupIndex) {
    Out[gi] = SrcBuffer[gi] * 2.0;
}

// Or, with a real consumer:
groupshared float g_Tile[64];

[numthreads(64, 1, 1)]
void cs_lds_used(uint gi : SV_GroupIndex) {
    g_Tile[gi] = (float)gi;
    GroupMemoryBarrierWithGroupSync();
    Out[gi] = g_Tile[(gi + 1) % 64] * 2.0;  // cross-thread read justifies LDS
}
```

## Options

none

## Fix availability

**suggestion** — Removing a groupshared write may also allow removing the surrounding barrier and the array declaration itself. The diagnostic identifies the dead write and points at the array declaration; the author decides how far to propagate the cleanup.

## See also

- Related rule: [groupshared-overwrite-before-barrier](groupshared-overwrite-before-barrier.md) — write later overwritten before any barrier
- Related rule: [groupshared-too-large](groupshared-too-large.md) — LDS budget pressure
- Related rule: [unused-cbuffer-field](unused-cbuffer-field.md) — sibling def-use rule for cbuffer fields
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-dead-store.md)

*© 2026 NelCit, CC-BY-4.0.*
