---
title: "buffer-load-width-vs-cache-line: A scalar `Load` (or `operator[]` returning a single element) on a `ByteAddressBuffer` / `StructuredBuffer<T>`…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: buffer-load-width-vs-cache-line
---

# buffer-load-width-vs-cache-line

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/buffer-load-width-vs-cache-line.md](../rules/buffer-load-width-vs-cache-line.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU memory hierarchies are designed for wide transactions per wave. AMD RDNA 2/3's L1 vector cache delivers data in 64-byte cache lines; NVIDIA Turing/Ada Lovelace's L1 caches in 128-byte lines; Intel Xe-HPG's L1 sampler/UAV cache uses 64-byte lines. When a wave's lanes issue 32 (RDNA wave32, NVIDIA, Xe-HPG) or 64 (RDNA wave64) per-lane scalar loads at consecutive byte offsets, the memory subsystem either coalesces them into one or two cache-line transactions automatically (best case) or issues N separate transactions (worst case, when the compiler cannot prove coalescibility).

## What the rule fires on

A scalar `Load` (or `operator[]` returning a single element) on a `ByteAddressBuffer` / `StructuredBuffer<T>` whose access pattern aggregates across a wave to a contiguous cache-line-sized region. Each lane is loading 4 bytes, the wave's lanes are loading consecutive 4-byte slots, and the total fetch size matches a 64-byte (RDNA cache line) or 128-byte (Turing/Ada L1 line) transaction that would coalesce into a single `Load4` per lane group. The Phase 7 IR-level per-wave aggregation analysis identifies the pattern.

See the [What it detects](../rules/buffer-load-width-vs-cache-line.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/buffer-load-width-vs-cache-line.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[buffer-load-width-vs-cache-line.md -> Examples](../rules/buffer-load-width-vs-cache-line.md#examples).

## See also

- [Rule page](../rules/buffer-load-width-vs-cache-line.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
