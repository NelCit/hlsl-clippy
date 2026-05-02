---
title: "structured-buffer-stride-not-cache-aligned: A `StructuredBuffer<T>` or `RWStructuredBuffer<T>` whose element stride (computed by Slang reflection from the HLSL…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: structured-buffer-stride-not-cache-aligned
---

# structured-buffer-stride-not-cache-aligned

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/structured-buffer-stride-not-cache-aligned.md](../rules/structured-buffer-stride-not-cache-aligned.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU L1 caches are line-organised. AMD RDNA 2/3 vector L1 is 64-byte lines; NVIDIA Turing/Ada L1 is 128-byte lines; Intel Xe-HPG is 64-byte lines. When a wave loads element `i` and element `i+1` from a `StructuredBuffer<T>`, the two loads hit the same cache line only if `stride * 2 <= line_size` and the elements are contiguous within a line. A stride of 24 bytes against a 64-byte line means three elements fit per line in one configuration (offsets 0/24/48) but the next three straddle (72/96/120 -> lines starting at 64 and 128). Across a wave of 32 lanes reading consecutive elements at stride 24, every wave reads from at least 12 distinct cache lines instead of the 12 minimum for stride-32 (which packs cleanly into 64-byte lines).

## What the rule fires on

A `StructuredBuffer<T>` or `RWStructuredBuffer<T>` whose element stride (computed by Slang reflection from the HLSL packing rules applied to `T`) is a multiple of 4 but not a multiple of the configured cache-line target (default 32 bytes; configurable to 16, 32, 64, or 128). The detector uses reflection rather than an AST scan because the actual stride includes implicit padding the source layout does not show. It does not fire on stride 16, 32, 64, 128 etc.; it fires on strides like 12, 20, 24, 28, 36, 40, 48 (multiple of 4 but co-prime with the cache-line target divided by 4).

See the [What it detects](../rules/structured-buffer-stride-not-cache-aligned.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/structured-buffer-stride-not-cache-aligned.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[structured-buffer-stride-not-cache-aligned.md -> Examples](../rules/structured-buffer-stride-not-cache-aligned.md#examples).

## See also

- [Rule page](../rules/structured-buffer-stride-not-cache-aligned.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
