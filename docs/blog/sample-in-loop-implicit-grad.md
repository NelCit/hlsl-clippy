---
title: "sample-in-loop-implicit-grad"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: sample-in-loop-implicit-grad
---

# sample-in-loop-implicit-grad

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sample-in-loop-implicit-grad.md](../rules/sample-in-loop-implicit-grad.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Pixel shaders execute as 2x2 quads â€” the smallest unit at which the rasterizer guarantees neighbouring fragments are co-resident on the same SIMD lanes. Implicit-derivative texture sampling computes mip selection by differencing the texture coordinate against the three other lanes in the quad. On AMD RDNA 2/3, this is implemented by the `image_sample` instruction reading `S#` and `T#` operands across the four quad lanes through the cross-lane permute network. On NVIDIA Turing and Ada, the texture unit (TMU) consumes coordinates from all four quad lanes in parallel and forms the partial derivatives in dedicated derivative-computation hardware before issuing the actual fetch. On Intel Xe-HPG, the same quad-coupled fetch protocol applies through the sampler subsystem.

## What the rule fires on

Calls to `Texture2D::Sample`, `Texture2DArray::Sample`, `TextureCube::Sample`, and the other `Sample` overloads that compute texture LOD from implicit screen-space derivatives, when the call appears inside a loop, inside a branch whose condition is not provably uniform across the pixel quad, or inside a non-inlined function whose call sites mix uniform and non-uniform contexts. The same pattern applies to `SampleBias` and `SampleCmp` because both still rely on implicit `ddx`/`ddy` of the texture coordinate. The rule does not fire on `SampleLevel`, `SampleGrad`, or `Load`, which carry their own LOD information and do not depend on cross-lane derivatives.

See the [What it detects](../rules/sample-in-loop-implicit-grad.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sample-in-loop-implicit-grad.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sample-in-loop-implicit-grad.md -> Examples](../rules/sample-in-loop-implicit-grad.md#examples).

## See also

- [Rule page](../rules/sample-in-loop-implicit-grad.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
