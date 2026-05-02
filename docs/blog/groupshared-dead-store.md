---
title: "groupshared-dead-store"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-dead-store
---

# groupshared-dead-store

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-dead-store.md](../rules/groupshared-dead-store.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

LDS bandwidth and LDS occupancy are both finite and tightly budgeted. On AMD RDNA 2/3, each compute unit ships ~64 KB of LDS shared across all in-flight wavefronts; on NVIDIA Ada Lovelace, each SM provides 100 KB of unified L1/shared with the shared partition tunable per kernel; on Intel Xe-HPG, the SLM allocation per Xe-core is similar in scale. Every dead store still consumes an LDS write port for one cycle, still occupies a slot in the LDS write coalescer, and (more importantly) keeps the corresponding cell live for occupancy-budgeting purposes â€” the compiler cannot reclaim an LDS region that the source code still references. A kernel declared with a 16 KB groupshared array will be budgeted for 16 KB of occupancy footprint regardless of whether the writes are observed, capping wave concurrency on the CU/SM.

## What the rule fires on

A write to a `groupshared` cell where, on every CFG path from the store to workgroup exit, no read of that cell is reached â€” neither by the writing thread nor by any other thread (the rule conservatively assumes any thread can read any cell unless the index is provably thread-local). The simplest case is a write to `gs[gi] = expr;` followed by no further reads of `gs` anywhere in the kernel body. Compound cases include writes whose only downstream read is itself dead (transitive dead stores) and writes overwritten on every subsequent path before any barrier (caught more specifically by [groupshared-overwrite-before-barrier](groupshared-overwrite-before-barrier.md)).

See the [What it detects](../rules/groupshared-dead-store.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-dead-store.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-dead-store.md -> Examples](../rules/groupshared-dead-store.md#examples).

## See also

- [Rule page](../rules/groupshared-dead-store.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
