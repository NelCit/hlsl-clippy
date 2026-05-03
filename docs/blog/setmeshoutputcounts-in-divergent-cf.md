---
title: "setmeshoutputcounts-in-divergent-cf"
date: 2026-05-02
author: shader-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: setmeshoutputcounts-in-divergent-cf
---

# setmeshoutputcounts-in-divergent-cf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/setmeshoutputcounts-in-divergent-cf.md](../rules/setmeshoutputcounts-in-divergent-cf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A mesh shader replaces the traditional vertex/hull/domain/geometry pipeline with a single compute-style entry point that produces a small, bounded array of vertices and primitives. `SetMeshOutputCounts` tells the runtime how many of each will actually be written. The hardware uses this count to allocate output buffers downstream (parameter cache, primitive setup, attribute interpolation), to set up the rasteriser's primitive walker, and to issue fixed-function clip / cull work. The contract requires the call to happen exactly once, in thread-uniform control flow, before any `SetMeshOutputs` or `SetMeshPrimitives` write. Calling it from divergent control flow means different threads in the group disagree on the output sizing; calling it twice means the second call's values overwrite the first while output writes may have already started against the original allocation.

## What the rule fires on

Calls to `SetMeshOutputCounts(vertexCount, primitiveCount)` inside a mesh shader that are reachable from a non-thread-uniform branch (any `if`, `for`, `while`, or `switch` whose predicate depends on `SV_GroupThreadID`, `SV_DispatchThreadID`, or any per-thread varying value), or `SetMeshOutputCounts` calls that can execute more than once per shader invocation along any control-flow path. Both forms are explicit undefined behaviour in the D3D12 mesh-shader specification.

See the [What it detects](../rules/setmeshoutputcounts-in-divergent-cf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/setmeshoutputcounts-in-divergent-cf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[setmeshoutputcounts-in-divergent-cf.md -> Examples](../rules/setmeshoutputcounts-in-divergent-cf.md#examples).

## See also

- [Rule page](../rules/setmeshoutputcounts-in-divergent-cf.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
