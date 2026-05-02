---
title: "dispatchmesh-grid-too-small-for-wave"
date: 2026-05-02
author: hlsl-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: dispatchmesh-grid-too-small-for-wave
---

# dispatchmesh-grid-too-small-for-wave

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/dispatchmesh-grid-too-small-for-wave.md](../rules/dispatchmesh-grid-too-small-for-wave.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Dispatching less than one wave wastes the entire dispatch: the lanes beyond the grid still consume a full wave slot on every IHV. RDNA 2/3/4 wave32 has 32 lanes; Turing+ always 32. AMD's RDNA Performance Guide flags sub-wave-sized dispatches as a measurable foot-gun for amplification shaders (mesh-shading frontend) where workload sizing is often handcoded.

## What the rule fires on

A `DispatchMesh(x, y, z)` call where all three arguments are integer literals and `x * y * z < expected_wave_size_for_target`.

See the [What it detects](../rules/dispatchmesh-grid-too-small-for-wave.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/dispatchmesh-grid-too-small-for-wave.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[dispatchmesh-grid-too-small-for-wave.md -> Examples](../rules/dispatchmesh-grid-too-small-for-wave.md#examples).

## See also

- [Rule page](../rules/dispatchmesh-grid-too-small-for-wave.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
