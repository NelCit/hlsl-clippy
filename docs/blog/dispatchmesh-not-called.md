---
title: "dispatchmesh-not-called"
date: 2026-05-02
author: hlsl-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: dispatchmesh-not-called
---

# dispatchmesh-not-called

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/dispatchmesh-not-called.md](../rules/dispatchmesh-not-called.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The amplification stage in the D3D12 mesh pipeline is contractually required to call `DispatchMesh` exactly once per launched thread group. The call hands control (and an optional payload) from the amplification phase to the mesh-shader phase. If the call is missed on any thread group's execution path, the hardware behaviour is undefined: AMD RDNA 2/3 may deadlock the geometry front-end waiting for the launch, NVIDIA Ada may silently drop the meshlet (the visual symptom is missing geometry on a fraction of frames), and Intel Xe-HPG may surface a TDR. The reproducibility is platform- and driver-dependent, which makes the bug a classic "works on the dev's machine, breaks in QA" hazard.

## What the rule fires on

An amplification shader entry point (`[shader("amplification")]` or any function with the amplification stage tag in reflection) where at least one CFG path from entry to function return does not call `DispatchMesh(x, y, z, payload)`. The rule fires on early returns guarded by per-thread conditions, on switch arms missing the dispatch call, and on loops whose exit path bypasses the dispatch. CFG analysis treats `DispatchMesh` as required-to-execute exactly once per amplification thread group.

See the [What it detects](../rules/dispatchmesh-not-called.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/dispatchmesh-not-called.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[dispatchmesh-not-called.md -> Examples](../rules/dispatchmesh-not-called.md#examples).

## See also

- [Rule page](../rules/dispatchmesh-not-called.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
