---
title: "wave64-on-rdna4-compute-misses-dynamic-vgpr"
date: 2026-05-02
author: shader-clippy maintainers
category: rdna4
tags: [hlsl, performance, rdna4]
status: stub
related-rule: wave64-on-rdna4-compute-misses-dynamic-vgpr
---

# wave64-on-rdna4-compute-misses-dynamic-vgpr

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md](../rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Per AMD's RDNA 4 deep-dives (Hot Chips 2025; Chips and Cheese RDNA 4), the new dynamic-VGPR allocation mode is wave32-only -- the per-wave `s_alloc_vgpr` instruction works only for the wave32 lane width. wave64 compute on RDNA 4 silently misses the per-block occupancy gain that dynamic-VGPR mode provides over the static allocation on RDNA 3.

## What the rule fires on

A compute entry point declared `[WaveSize(64)]` (or `[WaveSize(64, 64)]`) under the `[experimental.target = rdna4]` config gate.

See the [What it detects](../rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave64-on-rdna4-compute-misses-dynamic-vgpr.md -> Examples](../rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md#examples).

## See also

- [Rule page](../rules/wave64-on-rdna4-compute-misses-dynamic-vgpr.md) -- canonical reference + change log.
- [rdna4 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*

**TODO:** category-overview missing for `rdna4`; linked overview is the closest sibling.