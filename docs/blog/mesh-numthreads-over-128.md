---
title: "mesh-numthreads-over-128"
date: 2026-05-02
author: shader-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: mesh-numthreads-over-128
---

# mesh-numthreads-over-128

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mesh-numthreads-over-128.md](../rules/mesh-numthreads-over-128.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mesh and amplification shaders run on the same compute-style backend used for compute shaders, but the pipeline reserves a specific resource budget per workgroup: per-workgroup payload memory (16 KB for AS), per-workgroup vertex/primitive output memory (the output declaration cap), and a thread cap chosen so the whole pipeline can guarantee in-order delivery to the rasterizer. On NVIDIA Turing and Ada Lovelace, the mesh/AS dispatch path uses a fixed-size scoreboard slot per group; on AMD RDNA 2/3, the mesh shader runs as a primitive-shader-style workgroup that the rasterizer drains in lockstep; on Intel Xe-HPG, the pipeline budgets a per-group launch quantum sized to the 128-thread cap. The 128 ceiling is the contract that all three IHVs and the D3D12 runtime agreed on.

## What the rule fires on

A mesh-shader or amplification-shader entry point whose `[numthreads(X, Y, Z)]` attribute multiplies out to more than 128 threads per group. The D3D12 mesh-pipeline specification caps the per-group thread count at 128 for both stages; values above the cap fail PSO creation. The rule constant-folds the three integer arguments and fires when `X * Y * Z > 128` on a function annotated `[shader("mesh")]` or `[shader("amplification")]`.

See the [What it detects](../rules/mesh-numthreads-over-128.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mesh-numthreads-over-128.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mesh-numthreads-over-128.md -> Examples](../rules/mesh-numthreads-over-128.md#examples).

## See also

- [Rule page](../rules/mesh-numthreads-over-128.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
