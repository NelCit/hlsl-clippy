---
title: "missing-ray-flag-cull-non-opaque"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: missing-ray-flag-cull-non-opaque
---

# missing-ray-flag-cull-non-opaque

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/missing-ray-flag-cull-non-opaque.md](../rules/missing-ray-flag-cull-non-opaque.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

DXR traversal on every modern IHV (NVIDIA Turing/Ada Lovelace RT cores, AMD RDNA 2/3 Ray Accelerators, Intel Xe-HPG RTU) splits BVH leaf processing into two paths: the *opaque path*, where the leaf primitive is accepted directly by the traversal hardware, and the *non-opaque path*, where the hardware suspends traversal, returns to the SIMT engine, runs the any-hit shader, and resumes. The opaque path stays inside the RT hardware end-to-end; the non-opaque path costs a full shader invocation per leaf hit, including the wave reformation and the trip back through the scheduler. NVIDIA's Ada RT-core whitepaper measures the per-non-opaque-hit cost at roughly 30-60 ALU cycles of overhead on top of the any-hit shader's own work, even when the any-hit body is empty.

## What the rule fires on

`TraceRay(...)` or `RayQuery::TraceRayInline(...)` calls whose ray-flag argument does not include `RAY_FLAG_CULL_NON_OPAQUE` in a context where the bound any-hit shader is empty (returns immediately, only calls `IgnoreHit()`/`AcceptHitAndEndSearch()` unconditionally) or where reflection shows no any-hit shader is bound to the relevant hit groups. The rule reads the constant ray-flag value via tree-sitter constant-folding and uses Slang reflection to enumerate the hit groups in the pipeline subobject.

See the [What it detects](../rules/missing-ray-flag-cull-non-opaque.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/missing-ray-flag-cull-non-opaque.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[missing-ray-flag-cull-non-opaque.md -> Examples](../rules/missing-ray-flag-cull-non-opaque.md#examples).

## See also

- [Rule page](../rules/missing-ray-flag-cull-non-opaque.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
