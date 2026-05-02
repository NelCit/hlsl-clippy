---
title: "divergent-buffer-index-on-uniform-resource"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: divergent-buffer-index-on-uniform-resource
---

# divergent-buffer-index-on-uniform-resource

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/divergent-buffer-index-on-uniform-resource.md](../rules/divergent-buffer-index-on-uniform-resource.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Modern GPUs split memory paths between the *scalar* / *constant* cache and the *vector* L1. On AMD RDNA 2/3, a uniform-resource + uniform-index buffer load is issued as a scalar load through the K$, returning one value to the SGPR file at a fraction of the cost of a vector load. A uniform-resource + *divergent*-index load is forced onto the vector path: the hardware issues 32 (wave32) or 64 (wave64) parallel L1 transactions, one per lane. On NVIDIA Ada the constant-cache fast path requires both the resource and the offset be uniform; a divergent offset spills to the global L1 / L2 path and serialises by cache-line. On Intel Xe-HPG, the constant-buffer fast path likewise requires uniform offsets and the divergent case falls through to the data-port path, which serialises across distinct cache lines.

## What the rule fires on

An indexed buffer access `buf[i]` (where `buf` is a `Buffer`, `StructuredBuffer`, `ByteAddressBuffer`, or `ConstantBuffer<T>` and `i` is a wave-divergent expression) on a *resource binding* that is itself uniform across the wave â€” that is, `buf` is referenced through a single descriptor known at compile time, not through an `[NonUniformResourceIndex]` heap index. The hazard is the index, not the resource. The rule fires when the divergence analysis can prove the index varies across the wave (typical sources: `SV_DispatchThreadID`, per-lane loaded values, results of `WaveReadLaneAt`) while the resource itself is bound uniformly.

See the [What it detects](../rules/divergent-buffer-index-on-uniform-resource.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/divergent-buffer-index-on-uniform-resource.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[divergent-buffer-index-on-uniform-resource.md -> Examples](../rules/divergent-buffer-index-on-uniform-resource.md#examples).

## See also

- [Rule page](../rules/divergent-buffer-index-on-uniform-resource.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
