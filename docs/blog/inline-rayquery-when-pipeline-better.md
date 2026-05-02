---
title: "inline-rayquery-when-pipeline-better"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: inline-rayquery-when-pipeline-better
---

# inline-rayquery-when-pipeline-better

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/inline-rayquery-when-pipeline-better.md](../rules/inline-rayquery-when-pipeline-better.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

DXR offers two ray-tracing programming models. Pipeline ray tracing (`TraceRay` plus `[shader("anyhit"|"closesthit"|"miss"|"intersection")]` entry points) routes every traversal event through the shader table, allowing different geometry types to bind different shaders and enabling hardware Shader Execution Reordering (SER on NVIDIA Ada / Blackwell) to regroup divergent rays for SIMD efficiency. Inline ray queries (`RayQuery<RAY_FLAGS>` plus `Proceed()` / `CommittedXxx()` accessors) inline the entire traversal into the calling shader's body, eliminating the shader-table indirection but giving up the SER regrouping and forcing the caller to handle every traversal event in its own register file.

## What the rule fires on

Use of `RayQuery<>` (inline ray queries, SM 6.5+) in shaders where the workload characteristics make pipeline `TraceRay` the better choice â€” or, conversely, use of pipeline `TraceRay` for shaders where inline ray queries would be faster. The heuristic flags inline RQ inside pixel or compute shaders that traverse rays with high candidate counts (many alpha-tested any-hit calls), and flags pipeline RQ for shaders with simple shadow / AO / occlusion queries that have no any-hit work and a single uniform miss case.

See the [What it detects](../rules/inline-rayquery-when-pipeline-better.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/inline-rayquery-when-pipeline-better.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[inline-rayquery-when-pipeline-better.md -> Examples](../rules/inline-rayquery-when-pipeline-better.md#examples).

## See also

- [Rule page](../rules/inline-rayquery-when-pipeline-better.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
