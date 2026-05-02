---
title: "primcount-overrun-in-conditional-cf"
date: 2026-05-02
author: hlsl-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: primcount-overrun-in-conditional-cf
---

# primcount-overrun-in-conditional-cf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/primcount-overrun-in-conditional-cf.md](../rules/primcount-overrun-in-conditional-cf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mesh shaders on D3D12 (and the Vulkan / Metal equivalents) declare an upper bound on vertex and primitive output via `[outputtopology(...)]` plus a per-launch dynamic count via `SetMeshOutputCounts(maxVerts, maxPrims)`. The hardware allocates output storage for exactly that many primitives â€” on AMD RDNA 2/3 the mesh-shader output buffer is sized at compile time from the declared maxima and trimmed at launch by the dynamic call; on NVIDIA Ada the per-meshlet primitive ring is sized to the declared bound; on Intel Xe-HPG the equivalent output staging area is similarly sized. Writing past the declared count is undefined behaviour: the hardware may silently drop the over-count primitive, may overwrite a neighbouring meshlet's output (corrupting another lane group's geometry), or may surface a TDR / device-removed error if the IHV's runtime adds a bounds check on debug runtimes. The reproducibility varies by IHV and by driver version, which makes the bug a classic intermittent crash.

## What the rule fires on

A mesh-shader entry point that calls `SetMeshOutputCounts(v, p)` once at the top, then issues primitive index writes (typically `outIndices[i] = ...` or `triangleIndices[i] = ...`) inside conditional control flow whose join could push the live primitive count above `p` on at least one CFG path. The rule fires when CFG analysis can prove a path exists where `i >= p` reaches a primitive write. Companion to the locked [setmeshoutputcounts-in-divergent-cf](setmeshoutputcounts-in-divergent-cf.md), which targets the *call site* of `SetMeshOutputCounts` itself; this rule targets the *writes* on the consumer side.

See the [What it detects](../rules/primcount-overrun-in-conditional-cf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/primcount-overrun-in-conditional-cf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[primcount-overrun-in-conditional-cf.md -> Examples](../rules/primcount-overrun-in-conditional-cf.md#examples).

## See also

- [Rule page](../rules/primcount-overrun-in-conditional-cf.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
