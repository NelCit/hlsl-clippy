---
title: "wave-active-all-equal-precheck"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: wave-active-all-equal-precheck
---

# wave-active-all-equal-precheck

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave-active-all-equal-precheck.md](../rules/wave-active-all-equal-precheck.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

When a shader fetches from a resource array with a per-thread index, the hardware must perform one descriptor table walk per unique index in the wave. On AMD RDNA 2/3, divergent descriptor access uses the `S_LOAD_DWORDX4` cascade where the scalar unit scalarises the descriptor read across unique indices in the wave: 32 distinct indices means 32 sequential 16-byte descriptor loads, each blocking the wave on the L0 K$ cache. On NVIDIA Turing and Ada, divergent bindless access goes through the texture descriptor cache (TDC) and serialises similarly across unique handles. On Intel Xe-HPG, the bindless heap is read through the surface state heap with per-unique-handle BTI lookups. The throughput cost grows linearly with the number of unique indices in the wave, with the worst case at 32-64x the cost of a uniform fetch.

## What the rule fires on

Resource-array indexing patterns of the form `Textures[idx].Sample(...)` or `Buffers[idx].Load(...)` where `idx` is computed from per-thread data (instance ID, material ID buffered by `SV_VertexID`, dispatch thread ID) and the same shader does not first issue a `WaveActiveAllEqual(idx)` test to take a single uniform path when the wave happens to converge on one value. The rule also fires for indexed `cbuffer` array reads and for `NonUniformResourceIndex(idx)` calls that lack the precheck. Tile-based deferred renderers, GPU-driven culling pipelines, and bindless material systems are the typical sources.

See the [What it detects](../rules/wave-active-all-equal-precheck.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave-active-all-equal-precheck.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave-active-all-equal-precheck.md -> Examples](../rules/wave-active-all-equal-precheck.md#examples).

## See also

- [Rule page](../rules/wave-active-all-equal-precheck.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
