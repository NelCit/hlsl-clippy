---
title: "compute-dispatch-grid-shape-vs-quad"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: compute-dispatch-grid-shape-vs-quad
---

# compute-dispatch-grid-shape-vs-quad

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/compute-dispatch-grid-shape-vs-quad.md](../rules/compute-dispatch-grid-shape-vs-quad.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Shader Model 6.6 added compute-quad derivatives: in a compute shader, `ddx(v)` returns the lane-pair difference within a 2x2 quad of threads, just as in a pixel shader. The mechanism on every IHV requires the hardware to identify a 2x2 quad of lanes whose `SV_GroupThreadID` form an `(x, y)` adjacency. AMD RDNA 2/3 forms quads from lane indices `{0,1,2,3}`, `{4,5,6,7}`, ... within a wave; NVIDIA Turing/Ada use the same lane-pair adjacency for warp-level derivatives; Intel Xe-HPG forms quads from per-channel adjacency. In a 2D `[numthreads(8, 8, 1)]` group, the lane-to-quad mapping naturally pairs `SV_GroupThreadID.x` even/odd lanes and `SV_GroupThreadID.y` even/odd lanes, producing meaningful X and Y derivatives.

## What the rule fires on

A compute or amplification entry point declared with `[numthreads(N, 1, 1)]` (a 1D thread-group shape) whose body invokes `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, `ddx_coarse`, `ddy_coarse`, `QuadReadAcrossX`, `QuadReadAcrossY`, `QuadReadAcrossDiagonal`, or any compute-quad derivative-bearing intrinsic introduced in SM 6.6. The detector pulls the `[numthreads]` attribute via reflection / AST and scans the entry's reachable AST for the derivative intrinsics. It does not fire when the thread-group shape is 2D or 3D with both X and Y at least 2 (a real quad layout); it does not fire on pixel shaders (where derivatives are well-defined regardless of dispatch shape).

See the [What it detects](../rules/compute-dispatch-grid-shape-vs-quad.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/compute-dispatch-grid-shape-vs-quad.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[compute-dispatch-grid-shape-vs-quad.md -> Examples](../rules/compute-dispatch-grid-shape-vs-quad.md#examples).

## See also

- [Rule page](../rules/compute-dispatch-grid-shape-vs-quad.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
