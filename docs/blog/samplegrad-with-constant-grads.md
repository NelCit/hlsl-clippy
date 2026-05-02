---
title: "samplegrad-with-constant-grads"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: samplegrad-with-constant-grads
---

# samplegrad-with-constant-grads

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/samplegrad-with-constant-grads.md](../rules/samplegrad-with-constant-grads.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`SampleGrad` is the explicit-gradient variant of `Sample`. It accepts caller-supplied partial derivatives (ddx and ddy) so that the hardware LOD calculation uses those derivatives instead of computing them from the implicit 2x2 quad footprint. This is the right tool when derivatives are known analytically â€” for example, in a compute shader, inside a non-uniform control-flow block, or when sampling with custom UV transformations. The hardware TMU receives the gradient pair and computes `LOD = log2(max(length(ddx), length(ddy)))` to determine which mip level to sample.

## What the rule fires on

Calls to `SampleGrad(sampler, uv, ddx, ddy)` where both the `ddx` and `ddy` arguments are constant zero â€” either as `float2(0, 0)`, `float2(0.0, 0.0)`, `(float2)0`, or any expression that evaluates to a zero vector at compile time. The rule fires regardless of the texture type (`Texture2D`, `TextureCube`, `Texture2DArray`, etc.) and regardless of the UV dimensionality (`float2`, `float3`). It does not fire when either gradient argument is non-zero or when either argument is a runtime expression.

See the [What it detects](../rules/samplegrad-with-constant-grads.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/samplegrad-with-constant-grads.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[samplegrad-with-constant-grads.md -> Examples](../rules/samplegrad-with-constant-grads.md#examples).

## See also

- [Rule page](../rules/samplegrad-with-constant-grads.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
