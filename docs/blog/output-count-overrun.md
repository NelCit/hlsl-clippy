---
title: "output-count-overrun: Mesh-shader entry points that, at the IR level, write to `verts[i]` or `tris[i]` with…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: output-count-overrun
---

# output-count-overrun

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/output-count-overrun.md](../rules/output-count-overrun.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The mesh-shader output arrays are not ordinary HLSL arrays — they are bound to fixed-size on-chip groupshared regions whose extent is fixed at PSO compilation time from the `out vertices N` and `out indices M` attributes. On AMD RDNA 2/3, this region lives in the LDS (local data store); writing past the declared count typically clobbers neighbouring meshlet output state or, depending on driver and shader compiler version, silently writes nothing while the rasteriser consumes the truncated output. On NVIDIA Ada Lovelace, the same scenario produces undefined behaviour: the SM's mesh-shader output buffer has bank-conflict checks, but no bounds checks, and overruns can corrupt index data fed to the raster. Intel Xe-HPG validates only in development driver builds; release drivers permit the overrun and the resulting visual artefacts are typically diagnosed days later as "missing triangles" or "geometry pop-in."

## What the rule fires on

Mesh-shader entry points that, at the IR level, write to `verts[i]` or `tris[i]` with an index `i` whose statically known upper bound exceeds the corresponding declared `out vertices N` / `out indices M` capacity. The check also fires on the matching `SetMeshOutputCounts(v, p)` call when `v > N` or `p > M` for compile-time-constant arguments. The analysis runs after Slang's IR loop-bound and range propagation passes so it can resolve `for (uint i = 0; i < gtid + 1; ++i) verts[i] = ...` patterns where the index is bounded by a thread-id derived expression.

See the [What it detects](../rules/output-count-overrun.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/output-count-overrun.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[output-count-overrun.md -> Examples](../rules/output-count-overrun.md#examples).

## See also

- [Rule page](../rules/output-count-overrun.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
