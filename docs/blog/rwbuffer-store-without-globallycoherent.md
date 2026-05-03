---
title: "rwbuffer-store-without-globallycoherent"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: rwbuffer-store-without-globallycoherent
---

# rwbuffer-store-without-globallycoherent

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/rwbuffer-store-without-globallycoherent.md](../rules/rwbuffer-store-without-globallycoherent.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12 UAV memory traffic on every modern GPU is cached at the per-CU / per-SM L1 level by default. AMD RDNA 2/3 routes UAV writes through the per-CU L1 / VMEM cache; only when the cache line is evicted (LRU pressure) or explicitly flushed does the write reach L2 / HBM where another CU can see it. NVIDIA Ada caches UAV writes in the per-SM L1; the L2 is shared across SMs but the L1 is not, so a read on a different SM sees stale L1-cached data until a flush. Intel Xe-HPG behaves similarly with the per-Xe-core L1. The HLSL `globallycoherent` qualifier on a UAV declaration tells the compiler to bypass the per-unit L1 (or to flush after each write, depending on IHV), routing all access through L2 / shared coherent memory. Without that qualifier the cross-wave read may see arbitrary stale data â€” the bug is observable only on dispatches large enough to spill across multiple compute units, which makes it a classic "works in dev, fails in prod" hazard.

## What the rule fires on

A write to a `RWBuffer`, `RWStructuredBuffer`, `RWByteAddressBuffer`, or `RWTexture*` UAV `U` followed â€” within the *same* dispatch â€” by a read of `U` from a different wave / thread group with no `DeviceMemoryBarrier` / `AllMemoryBarrier` and no `globallycoherent` qualifier on the UAV declaration. The rule fires when the CFG analysis can prove the write and read are reachable in the same dispatch and the cross-wave consumer path exists. This is a D3D12-flavoured rule: Vulkan and Metal use different memory-model surfaces (subgroup uniform / non-coherent / Metal2 buffer atomics) for the same hazard, so the rule's surface here is specifically the HLSL `globallycoherent` qualifier.

See the [What it detects](../rules/rwbuffer-store-without-globallycoherent.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/rwbuffer-store-without-globallycoherent.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[rwbuffer-store-without-globallycoherent.md -> Examples](../rules/rwbuffer-store-without-globallycoherent.md#examples).

## See also

- [Rule page](../rules/rwbuffer-store-without-globallycoherent.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
