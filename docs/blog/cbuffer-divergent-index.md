---
title: "cbuffer-divergent-index: A read from a `cbuffer`, `ConstantBuffer<T>`, or inline constant buffer (ICB) where the field…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: cbuffer-divergent-index
---

# cbuffer-divergent-index

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cbuffer-divergent-index.md](../rules/cbuffer-divergent-index.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

cbuffer and constant-buffer data is served to shader threads through a dedicated constant-data path that is optimised for wave-uniform access. On AMD RDNA the constant data arrives via the scalar data cache (K-cache) and the scalar register file (SGPRs): all lanes in the wave share a single fetch, which is the correct and fast path when all lanes read the same index. On NVIDIA hardware, NVIDIA's developer documentation explicitly identifies divergent constant buffer reads as a serialization hazard: when lanes in a warp read different indices into a constant buffer array, the hardware cannot serve them as a single broadcast. Instead, the constant cache performs the reads sequentially, one unique index at a time, turning a single-cycle broadcast into a serialized sequence of up to 32 (or 64 on Turing with 2x warp scheduling) individual constant loads.

## What the rule fires on

A read from a `cbuffer`, `ConstantBuffer<T>`, or inline constant buffer (ICB) where the field is selected through an index that is per-lane divergent — for example, `cb.array[lane_idx]` where `lane_idx` is derived from a semantic input (`SV_InstanceID`, `TEXCOORD`, or similar) or from a wave-divergent computation. The rule relies on Slang's uniformity analysis to determine whether the index is wave-uniform or potentially divergent. It does not fire on compile-time-constant indices or on indices that Slang can prove are uniform across all lanes in the wave.

See the [What it detects](../rules/cbuffer-divergent-index.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cbuffer-divergent-index.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cbuffer-divergent-index.md -> Examples](../rules/cbuffer-divergent-index.md#examples).

## See also

- [Rule page](../rules/cbuffer-divergent-index.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
