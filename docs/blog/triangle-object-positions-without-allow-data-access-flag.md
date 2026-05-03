---
title: "triangle-object-positions-without-allow-data-access-flag"
date: 2026-05-02
author: shader-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: triangle-object-positions-without-allow-data-access-flag
---

# triangle-object-positions-without-allow-data-access-flag

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/triangle-object-positions-without-allow-data-access-flag.md](../rules/triangle-object-positions-without-allow-data-access-flag.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`TriangleObjectPositions()` requires the underlying acceleration structure to have been built with `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` (D3D12) or `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR` (Vulkan). Without the flag, the call is undefined behaviour: drivers may return garbage positions, segfault, or silently fall back to an older BVH layout.

## What the rule fires on

Every call site of `TriangleObjectPositions()` (SM 6.10 ray-tracing intrinsic). Project-side state (the BLAS-build flag) is invisible from shader source, so the rule fires on every call site as a reminder.

See the [What it detects](../rules/triangle-object-positions-without-allow-data-access-flag.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/triangle-object-positions-without-allow-data-access-flag.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[triangle-object-positions-without-allow-data-access-flag.md -> Examples](../rules/triangle-object-positions-without-allow-data-access-flag.md#examples).

## See also

- [Rule page](../rules/triangle-object-positions-without-allow-data-access-flag.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
