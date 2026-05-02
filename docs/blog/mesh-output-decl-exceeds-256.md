---
title: "mesh-output-decl-exceeds-256"
date: 2026-05-02
author: hlsl-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: mesh-output-decl-exceeds-256
---

# mesh-output-decl-exceeds-256

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mesh-output-decl-exceeds-256.md](../rules/mesh-output-decl-exceeds-256.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mesh-shader output buffers live in a fixed per-group slot allocated by the pipeline at workgroup launch time. On NVIDIA Turing and Ada Lovelace, the per-group output region is sized for 256 vertices and 256 primitives multiplied by the configured per-vertex output stride. On AMD RDNA 2/3, the mesh shader writes through a primitive-shader pipeline that uses an LDS-resident output region carved at the same cap. Intel Xe-HPG (Arc Alchemist, Battlemage) implements the same 256/256 ceiling as part of its mesh-pipeline conformance to the D3D12 spec.

## What the rule fires on

A mesh-shader entry point whose `out vertices` or `out indices` array declarations exceed 256 elements in either dimension. The D3D12 mesh-pipeline specification caps both per-group output declarations at 256: a maximum of 256 vertices and 256 primitives. The rule constant-folds the array size literals on the `out vertices` / `out indices` parameters and fires when either exceeds 256 on a function annotated `[shader("mesh")]`.

See the [What it detects](../rules/mesh-output-decl-exceeds-256.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mesh-output-decl-exceeds-256.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mesh-output-decl-exceeds-256.md -> Examples](../rules/mesh-output-decl-exceeds-256.md#examples).

## See also

- [Rule page](../rules/mesh-output-decl-exceeds-256.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
