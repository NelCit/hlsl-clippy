---
title: "quadany-quadall-non-quad-stage"
date: 2026-05-02
author: shader-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: quadany-quadall-non-quad-stage
---

# quadany-quadall-non-quad-stage

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/quadany-quadall-non-quad-stage.md](../rules/quadany-quadall-non-quad-stage.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Quad intrinsics rely on the hardware launching threads in 2x2 groups so the cross-lane reads (`QuadReadAcross*`) and the per-quad reductions (`QuadAny`/`QuadAll`) have well-defined neighbours. On NVIDIA Turing/Ada Lovelace, the rasterizer guarantees this for pixel shaders by construction (the quad is the rasterizer's primitive), and SM 6.6+ compute supports it when the `[numthreads]` X and Y are even. AMD RDNA 2/3 and Intel Xe-HPG follow the same contract.

## What the rule fires on

A call to `QuadAny(...)`, `QuadAll(...)`, `QuadReadAcrossX`, `QuadReadAcrossY`, or `QuadReadAcrossDiagonal` from an entry stage that does not provide quad semantics. The SM 6.7 quad intrinsics are defined for pixel shaders and for compute shaders launched with a `[numthreads(X, Y, 1)]` shape that produces well-formed 2x2 quads (X and Y both multiples of 2, total threads >= 4). Slang reflection identifies the stage; the rule fires on quad calls from vertex, hull, domain, geometry, mesh, amplification, raygen, closest-hit, any-hit, miss, intersection, and callable shaders, plus from compute shaders whose `[numthreads]` does not satisfy the quad-shape requirement.

See the [What it detects](../rules/quadany-quadall-non-quad-stage.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/quadany-quadall-non-quad-stage.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[quadany-quadall-non-quad-stage.md -> Examples](../rules/quadany-quadall-non-quad-stage.md#examples).

## See also

- [Rule page](../rules/quadany-quadall-non-quad-stage.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
