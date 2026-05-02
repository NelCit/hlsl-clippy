---
title: "hitobject-invoke-after-recursion-cap"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: hitobject-invoke-after-recursion-cap
---

# hitobject-invoke-after-recursion-cap

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/hitobject-invoke-after-recursion-cap.md](../rules/hitobject-invoke-after-recursion-cap.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The DXR pipeline's `D3D12_RAYTRACING_PIPELINE_CONFIG.MaxTraceRecursionDepth` declares an upper bound on how deep the trace stack can grow. The runtime uses this bound to size the per-lane ray stack, which is shared between `TraceRay`-style recursion and `HitObject::Invoke`-style deferred invocation. On NVIDIA Ada Lovelace, AMD RDNA 3/4, and Intel Xe-HPG (with the OMM extension), exceeding the declared depth at runtime is undefined behaviour: the hardware may corrupt adjacent stack frames, fault, or silently truncate.

## What the rule fires on

A `dx::HitObject::Invoke(...)` call reachable from a closest-hit shader chain whose nominal recursion depth exceeds the pipeline's `MaxTraceRecursionDepth`. The Phase 4 call-graph + recursion-budget analysis walks the trace chain (raygen -> closest-hit -> potentially another `Invoke` -> closest-hit -> ...) and accumulates the depth.

See the [What it detects](../rules/hitobject-invoke-after-recursion-cap.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/hitobject-invoke-after-recursion-cap.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[hitobject-invoke-after-recursion-cap.md -> Examples](../rules/hitobject-invoke-after-recursion-cap.md#examples).

## See also

- [Rule page](../rules/hitobject-invoke-after-recursion-cap.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
