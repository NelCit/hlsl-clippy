---
title: "meshlet-vertex-count-bad"
date: 2026-05-02
author: shader-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: meshlet-vertex-count-bad
---

# meshlet-vertex-count-bad

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/meshlet-vertex-count-bad.md](../rules/meshlet-vertex-count-bad.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mesh shaders dispatch a wave per meshlet, and the wave's vertex output is held in on-chip groupshared memory until the rasteriser consumes it. AMD RDNA 2 and RDNA 3 use SIMD32 waves and want meshlet vertex counts that are exact multiples of the wave width: 32, 64, 96, or 128 vertices map to 1, 2, 3, or 4 vertex-shading iterations per wave. The 96-vertex case is the trap â€” it requires three iterations, none of which can fully utilise the 32-lane SIMD when triangle indices reference vertices unevenly. AMD's mesh-shader guidance recommends 64 vertices and 124 primitives as the default sweet spot for RDNA 2/3, with 128 vertices acceptable when the meshlet is geometry-dense.

## What the rule fires on

Mesh-shader entry points whose `[outputtopology(...)]` + `out vertices N` + `out indices M` declarations choose meshlet sizes outside the per-vendor sweet-spot range. The rule fires when `N` (vertex count) or `M` (index count, expressed as triangles) lands in a known-pathological band â€” for example, 96 vertices on RDNA 2/3 (which prefers powers of two up to 64 or 128) or 128 vertices on NVIDIA Ada Lovelace (which prefers 64 for compute-bound passes and 128 only when index-bandwidth-bound). The check consults a small built-in table of vendor sweet spots derived from public driver guidance and AMD/NVIDIA mesh-shader best-practice documents.

See the [What it detects](../rules/meshlet-vertex-count-bad.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/meshlet-vertex-count-bad.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[meshlet-vertex-count-bad.md -> Examples](../rules/meshlet-vertex-count-bad.md#examples).

## See also

- [Rule page](../rules/meshlet-vertex-count-bad.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
