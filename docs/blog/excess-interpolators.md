---
title: "excess-interpolators"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: excess-interpolators
---

# excess-interpolators

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/excess-interpolators.md](../rules/excess-interpolators.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Interpolated vertex attributes are not free: every `TEXCOORDn` slot occupies dedicated hardware storage between the rasteriser and the pixel shader. On AMD RDNA, RDNA 2, and RDNA 3, post-rasterisation attribute data is staged in the LDS (Local Data Share) of the CU running the pixel shader. The rasteriser writes per-vertex attribute values into LDS at primitive setup time; the pixel shader's parameter cache then reads barycentric-weighted samples from those LDS-resident values. LDS is a finite resource â€” 64 KB per workgroup processor on RDNA 2/3 â€” and is shared with groupshared memory, GS/HS export buffers, and the parameter cache itself. Interpolator pressure directly contests the same LDS pool that compute kernels and amplification shaders draw from, lowering pixel-shader wave occupancy on attribute-heavy workloads.

## What the rule fires on

A vertex-to-pixel (or vertex-to-geometry, vertex-to-hull, mesh-to-pixel) interface struct whose total occupied `TEXCOORDn` slot count exceeds the configured hardware interpolator budget (default: 16 vec4 slots, matching the D3D12 SM6.x maximum of 32 four-component scalars across all input semantics counted per the runtime packing rules). The rule sums every member's slot footprint â€” `float4` consumes 1 slot, `float3`/`float2`/`float` round up to 1 slot each unless the compiler successfully packs them, and matrices and arrays multiply by their row/element count. The diagnostic fires on the struct declaration itself, naming the total slot count and listing the largest contributors.

See the [What it detects](../rules/excess-interpolators.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/excess-interpolators.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[excess-interpolators.md -> Examples](../rules/excess-interpolators.md#examples).

## See also

- [Rule page](../rules/excess-interpolators.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
