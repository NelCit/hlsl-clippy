---
title: "recursion-depth-not-declared"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: recursion-depth-not-declared
---

# recursion-depth-not-declared

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/recursion-depth-not-declared.md](../rules/recursion-depth-not-declared.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`MaxTraceRecursionDepth` is not a hint â€” it is a hard sizing parameter the driver uses to allocate the per-lane ray stack at PSO creation time. The runtime multiplies the declared depth by the maximum payload + attribute + per-call live-state footprint to compute the stack size, then allocates that amount of scratch memory per lane in the launch grid. On a 1080p `DispatchRays` with one ray per pixel and 64-lane waves on RDNA 3, the ray-stack pool sized at `MaxTraceRecursionDepth = 8` versus `MaxTraceRecursionDepth = 2` is a four-times difference in allocated scratch â€” easily tens of megabytes of VRAM that sits unused if the actual depth is shallower.

## What the rule fires on

DXR pipeline-state-object construction sites (in companion C++ source consumed by the linter, or in `[shader("raygeneration")]` entry-point metadata when authored in pure HLSL builds) that fail to set `MaxTraceRecursionDepth` on the `D3D12_RAYTRACING_PIPELINE_CONFIG` subobject. The rule additionally fires when the PSO sets a recursion depth that does not match the static call-graph depth of `TraceRay` calls observable in the linked shader set: too small a depth means runtime errors when a deep chain runs; too large a depth means the driver pre-allocates a larger ray stack than the shader will ever use.

See the [What it detects](../rules/recursion-depth-not-declared.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/recursion-depth-not-declared.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[recursion-depth-not-declared.md -> Examples](../rules/recursion-depth-not-declared.md#examples).

## See also

- [Rule page](../rules/recursion-depth-not-declared.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
