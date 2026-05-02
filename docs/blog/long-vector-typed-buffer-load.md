---
title: "long-vector-typed-buffer-load"
date: 2026-05-02
author: hlsl-clippy maintainers
category: long-vectors
tags: [hlsl, performance, long-vectors]
status: stub
related-rule: long-vector-typed-buffer-load
---

# long-vector-typed-buffer-load

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/long-vector-typed-buffer-load.md](../rules/long-vector-typed-buffer-load.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Typed buffers (`Buffer<float4>`, `RWBuffer<uint2>`, etc.) are texture-cache-backed views: the texture units on every IHV (NVIDIA's L1, AMD RDNA's TC, Intel Xe-HPG's L1 sampler cache) accept the load with a DXGI format descriptor, which the hardware uses to pick the right fetch shape and the right format conversion. Long vectors have no DXGI format equivalent â€” there is no `DXGI_FORMAT_R32G32B32A32B32C32D32E32F32_FLOAT` because the texture cache hardware was never designed for 32-byte fetches. The DXC validator rejects the type combination at PSO compile.

## What the rule fires on

A `Buffer<vector<T, N>>` declaration with `N >= 5`, or a typed-buffer `Load`/`operator[]` whose returned type is a long vector. The SM 6.9 long-vector spec restricts typed-buffer (`Buffer<T>` / `RWBuffer<T>`) views to the legacy 1/2/3/4-wide vector types because typed-buffer fetches go through the texture cache, which expects DXGI-format-compatible types. Long vectors must be loaded through `ByteAddressBuffer` or `StructuredBuffer`. Slang reflection identifies the resource type at binding sites; the rule fires when the typed-view's element type is a long vector.

See the [What it detects](../rules/long-vector-typed-buffer-load.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/long-vector-typed-buffer-load.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[long-vector-typed-buffer-load.md -> Examples](../rules/long-vector-typed-buffer-load.md#examples).

## See also

- [Rule page](../rules/long-vector-typed-buffer-load.md) -- canonical reference + change log.
- [long-vectors overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
