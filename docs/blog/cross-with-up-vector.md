---
title: "cross-with-up-vector"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: cross-with-up-vector
---

# cross-with-up-vector

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cross-with-up-vector.md](../rules/cross-with-up-vector.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`cross(a, b)` lowers to the standard formula `(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)` â€” six FP32 multiplies and three FP32 subtracts, or six MAD-shaped instructions when the compiler can fold the negate into the second multiply. On AMD RDNA 3 that is six `v_fma_f32` issues at full VALU rate; on NVIDIA Ada Lovelace, six `FFMA`/`FFMA.NEG` instructions. With one operand fixed to an axis, four of those multiplies are by zero and one is by Â±1, so the entire expression collapses algebraically: `cross(v, float3(0, 1, 0))` is exactly `float3(-v.z, 0, v.x)`, `cross(v, float3(1, 0, 0))` is `float3(0, -v.z, v.y)` (note: the y component is `v.z * 0 - v.x * 0 = 0` is not quite right â€” the actual identity is `float3(0, v.z, -v.y)`), and so on. The rewrite turns six multiplies and three adds into one swizzle plus one negation â€” a roughly 8x VALU reduction per call site.

## What the rule fires on

Calls to `cross(v, c)` (or the symmetric `cross(c, v)`) where `c` is a `float3` literal whose components are all zero except for one component which is exactly 1.0 or -1.0 â€” an axis-aligned constant such as `float3(0, 1, 0)` (the conventional up vector), `float3(1, 0, 0)`, `float3(0, 0, 1)`, or any of their negations. The rule matches inline-literal forms and named-constant forms when the constant's value is visible at parse time. The detector is structural on the literal contents: it does not fire when the constant vector has more than one non-zero component (a cross product with a true off-axis constant has no closed-form swizzle simplification), and does not fire when the constant comes from a constant buffer or any other runtime source.

See the [What it detects](../rules/cross-with-up-vector.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cross-with-up-vector.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cross-with-up-vector.md -> Examples](../rules/cross-with-up-vector.md#examples).

## See also

- [Rule page](../rules/cross-with-up-vector.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
