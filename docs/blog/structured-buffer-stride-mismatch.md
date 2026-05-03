---
title: "structured-buffer-stride-mismatch"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: structured-buffer-stride-mismatch
---

# structured-buffer-stride-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/structured-buffer-stride-mismatch.md](../rules/structured-buffer-stride-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`StructuredBuffer<T>` elements are accessed through the texture / L2 / L1 cache hierarchy, which operates on cache-line granularities of 64 or 128 bytes depending on the architecture. For efficient cache utilisation, element strides should align to 16-byte boundaries so that element boundaries coincide with the natural sub-cacheline boundaries used by gather/scatter units on RDNA and Turing hardware.

## What the rule fires on

A `StructuredBuffer<T>` or `RWStructuredBuffer<T>` declaration where the element type `T` has a byte size that is not a multiple of 16. The rule uses Slang's reflection API to determine `sizeof(T)` after HLSL struct-packing rules are applied, then checks `sizeof(T) % 16 != 0`. Common triggers: `StructuredBuffer<Particle>` where `Particle` contains only `float3 pos` (12 bytes, remainder 12); `StructuredBuffer<GpuLight>` where `GpuLight` has `float3 + float + float3 = 28 bytes`, remainder 12. See `tests/fixtures/phase3/bindings.hlsl`, lines 42â€“45 (`Particle`, 12 bytes) and `tests/fixtures/phase3/bindings_extra.hlsl`, lines 59â€“67 (`GpuLight`, 28 bytes).

See the [What it detects](../rules/structured-buffer-stride-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/structured-buffer-stride-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[structured-buffer-stride-mismatch.md -> Examples](../rules/structured-buffer-stride-mismatch.md#examples).

## See also

- [Rule page](../rules/structured-buffer-stride-mismatch.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
