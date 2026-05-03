---
title: "long-vector-bytebuf-load-misaligned"
date: 2026-05-02
author: shader-clippy maintainers
category: long-vectors
tags: [hlsl, performance, long-vectors]
status: stub
related-rule: long-vector-bytebuf-load-misaligned
---

# long-vector-bytebuf-load-misaligned

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/long-vector-bytebuf-load-misaligned.md](../rules/long-vector-bytebuf-load-misaligned.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Long-vector loads through `ByteAddressBuffer` lower to a sequence of widened scalar-cache fetches on every IHV. AMD RDNA 2/3's K$ delivers 64-byte cache lines; NVIDIA Ada's L1 scalar path is 128-byte; Intel Xe-HPG's scalar cache aligns to 64 bytes. When the long vector's start address is naturally aligned, the fetcher issues one or two cache-line transactions (depending on vector size); when it isn't, the fetcher issues N+1 transactions because the data straddles a line, and on stricter implementations the load may fault entirely.

## What the rule fires on

A `ByteAddressBuffer.Load<vector<T, N>>(offset)` with `N >= 5` whose constant-folded `offset` is not aligned to the natural alignment of the long-vector type (`N * sizeof(T)` rounded up to the IHV's preferred load width â€” 16 bytes for FP16/BF16, 32 bytes for FP32 long vectors with `N >= 8`). Constant-fold the offset; fire on misalignment.

See the [What it detects](../rules/long-vector-bytebuf-load-misaligned.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/long-vector-bytebuf-load-misaligned.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[long-vector-bytebuf-load-misaligned.md -> Examples](../rules/long-vector-bytebuf-load-misaligned.md#examples).

## See also

- [Rule page](../rules/long-vector-bytebuf-load-misaligned.md) -- canonical reference + change log.
- [long-vectors overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
