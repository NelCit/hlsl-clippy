---
title: "texture-lod-bias-without-grad: Calls to `SampleBias(sampler, uv, bias)` in any of these contexts: a compute shader (any…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: texture-lod-bias-without-grad
---

# texture-lod-bias-without-grad

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/texture-lod-bias-without-grad.md](../rules/texture-lod-bias-without-grad.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Implicit derivatives — the values returned by `ddx()` and `ddy()` — are computed by the hardware using the difference between the current lane's value and its horizontal or vertical neighbour lane within a 2x2 pixel quad. The quad is the fundamental unit of pixel shader execution on all current GPU architectures: AMD GCN through RDNA 3, NVIDIA Kepler through Ada Lovelace, and Intel Xe-HPG all dispatch pixel shaders in 2x2 quads to support finite-difference derivative computation. In compute shaders, no such quad structure exists — the notion of a neighbouring lane in pixel space is undefined. When `ddx(uv)` is evaluated in a compute shader, the compiler either inserts a zero (producing a derivative of zero, which maps to mip 0) or produces architecturally undefined results, depending on the driver and shader model version.

## What the rule fires on

Calls to `SampleBias(sampler, uv, bias)` in any of these contexts: a compute shader (any function decorated with `[numthreads]`), a function that does not execute in a pixel-shader quad (detected via Slang stage reflection), or a pixel shader function body where the UV argument is not quad-uniform (for example, when `uv` is computed inside a non-uniform branch that diverges within a 2x2 quad). `SampleBias` adds a floating-point offset to the hardware-computed LOD before selecting the mip level. The LOD itself is computed from the implicit derivatives `ddx(uv)` and `ddy(uv)`, which are only defined in pixel shader stage within a fully converged 2x2 quad. The rule does not fire in pixel shader entry points where the UV is demonstrably quad-uniform (e.g., derived from a linear interpolator with no per-lane divergence before the call).

See the [What it detects](../rules/texture-lod-bias-without-grad.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/texture-lod-bias-without-grad.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[texture-lod-bias-without-grad.md -> Examples](../rules/texture-lod-bias-without-grad.md#examples).

## See also

- [Rule page](../rules/texture-lod-bias-without-grad.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
