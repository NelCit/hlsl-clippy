---
title: "descriptor-heap-no-non-uniform-marker: A `ResourceDescriptorHeap[i]` or `SamplerDescriptorHeap[i]` access (SM 6.6 dynamic indexing into the descriptor heap) where…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: descriptor-heap-no-non-uniform-marker
---

# descriptor-heap-no-non-uniform-marker

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/descriptor-heap-no-non-uniform-marker.md](../rules/descriptor-heap-no-non-uniform-marker.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

SM 6.6 introduced direct `ResourceDescriptorHeap` indexing as a first-class HLSL feature. A heap index is said to be non-uniform when its value may differ across lanes of the same wave (i.e., it is per-lane divergent, not wave-uniform). The HLSL and DXIL specifications require that any non-uniform heap index be wrapped in `NonUniformResourceIndex` to signal this to the driver and hardware. Without the marker, the behaviour is explicitly undefined by the specification.

## What the rule fires on

A `ResourceDescriptorHeap[i]` or `SamplerDescriptorHeap[i]` access (SM 6.6 dynamic indexing into the descriptor heap) where the index `i` is a per-lane divergent value — typically a function parameter, a semantic input (`TEXCOORD`, `SV_InstanceID`), or any value derived from one — and the index is not wrapped in `NonUniformResourceIndex(...)`. The rule uses Slang's reflection and uniformity analysis to determine whether `i` could differ across lanes in the same wave. See `tests/fixtures/phase3/bindings_extra.hlsl`, lines 30–32 for the `get_material_texture` example and lines 35–38 for the correct `NonUniformResourceIndex` counterpart.

See the [What it detects](../rules/descriptor-heap-no-non-uniform-marker.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/descriptor-heap-no-non-uniform-marker.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[descriptor-heap-no-non-uniform-marker.md -> Examples](../rules/descriptor-heap-no-non-uniform-marker.md#examples).

## See also

- [Rule page](../rules/descriptor-heap-no-non-uniform-marker.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
