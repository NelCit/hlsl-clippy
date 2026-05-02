---
title: "reordercoherent-uav-missing-barrier: A UAV qualified `[reordercoherent]` whose write site reaches a read site across a `MaybeReorderThread`…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: reordercoherent-uav-missing-barrier
---

# reordercoherent-uav-missing-barrier

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/reordercoherent-uav-missing-barrier.md](../rules/reordercoherent-uav-missing-barrier.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`[reordercoherent]` is the SER-specific cousin of `globallycoherent`: it tells the runtime that the UAV's coherence has to survive a `MaybeReorderThread` event. The reorder physically rearranges lanes within the wave (and on some implementations across waves), so a write from "old lane 5" and a read from "new lane 5" are unrelated unless the runtime forces the L1 cache to flush the old write before the reorder. The `[reordercoherent]` qualifier is the contract that the application has placed an explicit barrier or fence around the reorder point; without one, the read sees stale data.

## What the rule fires on

A UAV qualified `[reordercoherent]` whose write site reaches a read site across a `MaybeReorderThread` reordering point on at least one CFG path, without an intervening barrier or memory-fence. The SER specification's `[reordercoherent]` qualifier promises the runtime that the application has explicit synchronisation around the reorder; missing that synchronisation is undefined behaviour because the reorder shuffles lanes between the write and the read in a way the cache hierarchy can no longer correlate. The Phase 4 analysis crosses CFG paths through the reorder point and verifies the barrier presence.

See the [What it detects](../rules/reordercoherent-uav-missing-barrier.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/reordercoherent-uav-missing-barrier.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[reordercoherent-uav-missing-barrier.md -> Examples](../rules/reordercoherent-uav-missing-barrier.md#examples).

## See also

- [Rule page](../rules/reordercoherent-uav-missing-barrier.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
