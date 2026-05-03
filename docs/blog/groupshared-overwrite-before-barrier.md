---
title: "groupshared-overwrite-before-barrier"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-overwrite-before-barrier
---

# groupshared-overwrite-before-barrier

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-overwrite-before-barrier.md](../rules/groupshared-overwrite-before-barrier.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU memory ordering for groupshared / LDS is barrier-relative: a write becomes visible to other lanes in the workgroup only after a `GroupMemoryBarrier*` is executed by both the writer and the reader. With no intervening barrier, a write that is shadowed by a later write from the same thread is unobservable to every other thread â€” the second write completely supersedes the first in the LDS bank, and no consumer wave ever sees the original value. On AMD RDNA 2/3 the LDS write coalescer may fold back-to-back same-address stores into a single transaction, but the wasted instruction issue still occupies the LDS write port. On NVIDIA Ada the shared-memory atomic / store unit serialises the two writes; on Intel Xe-HPG the SLM behaves similarly. In every case the first write costs a full LDS write cycle and produces zero observable effect.

## What the rule fires on

A write to a `groupshared` cell `gs[i]` followed by a second write to the same cell from the same thread on every CFG path, with no `GroupMemoryBarrier`, `GroupMemoryBarrierWithGroupSync`, `AllMemoryBarrier`, or equivalent intervening synchronisation that could have made the first write observable to other threads. The classic case is `gs[gi] = init_value; gs[gi] = computed_value;` in a straight-line block. The rule also fires when the second store is conditional but dominates the join (every path from the first write reaches the second).

See the [What it detects](../rules/groupshared-overwrite-before-barrier.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-overwrite-before-barrier.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-overwrite-before-barrier.md -> Examples](../rules/groupshared-overwrite-before-barrier.md#examples).

## See also

- [Rule page](../rules/groupshared-overwrite-before-barrier.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
