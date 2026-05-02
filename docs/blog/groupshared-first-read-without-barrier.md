---
title: "groupshared-first-read-without-barrier: A read of `gs[expr]` reachable from the kernel entry without any preceding `GroupMemoryBarrierWithGroupSync` (or…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-first-read-without-barrier
---

# groupshared-first-read-without-barrier

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-first-read-without-barrier.md](../rules/groupshared-first-read-without-barrier.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The HLSL / D3D12 memory model treats groupshared memory as unordered across lanes until a `GroupMemoryBarrier*` makes prior writes visible. Without that barrier, a lane reading `gs[some_other_lane_index]` is racing the lane that wrote that cell. The race is real on every modern GPU: AMD RDNA 2/3 issues LDS reads through the LDS read port without a wait on prior writes from sibling waves in the same workgroup; NVIDIA Ada's shared-memory unit similarly does not auto-fence cross-warp reads; Intel Xe-HPG SLM behaves identically. The hazard often hides behind a same-wave coincidence (within a single wave, lanes execute in lockstep on most IHVs, so a same-wave write-then-read happens to work), but as soon as a workgroup contains more than one wave, cross-wave reads see undefined values — sometimes the new write, sometimes the old.

## What the rule fires on

A read of `gs[expr]` reachable from the kernel entry without any preceding `GroupMemoryBarrierWithGroupSync` (or equivalent group-syncing barrier) on at least one CFG path, when `expr` may resolve to a cell that *some other thread* may have written. This is distinct from [groupshared-uninitialized-read](groupshared-uninitialized-read.md), which fires when *no thread* has written the cell on any path; this rule fires when one or more threads *have* written the cell but the reader cannot rely on those writes being visible because no barrier has executed in between.

See the [What it detects](../rules/groupshared-first-read-without-barrier.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-first-read-without-barrier.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-first-read-without-barrier.md -> Examples](../rules/groupshared-first-read-without-barrier.md#examples).

## See also

- [Rule page](../rules/groupshared-first-read-without-barrier.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
