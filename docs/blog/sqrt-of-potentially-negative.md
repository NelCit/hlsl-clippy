---
title: "sqrt-of-potentially-negative"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: sqrt-of-potentially-negative
---

# sqrt-of-potentially-negative

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sqrt-of-potentially-negative.md](../rules/sqrt-of-potentially-negative.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

IEEE 754 `sqrt(x)` for `x < 0` returns NaN. The same applies to `rsqrt(x)` for `x < 0` (and `rsqrt(0)` returns `+inf`). On AMD RDNA 2/3, `v_sqrt_f32` and `v_rsq_f32` are transcendental instructions that conform to IEEE for negative inputs by producing NaN; the bit pattern propagates through subsequent VALU operations. NVIDIA Turing, Ada, and Blackwell follow the same convention via `MUFU.SQRT` and `MUFU.RSQ`. Intel Xe-HPG's transcendental pipe is identical. The driver has no way to distinguish "this NaN was a bug" from "this NaN was deliberate", so the result is silently a NaN that contaminates the frame.

## What the rule fires on

Calls to `sqrt(x)` (and `rsqrt(x)`) where `x` is a signed expression whose value can be negative on plausible inputs: a subtraction `a - b`, a `1.0 - dot(v, v)` style discriminant where `v` may not be unit-length, a discriminant in a quadratic solver (`b*b - 4*a*c`) without a non-negative guard, or any expression involving a buffer load that the rule cannot prove is non-negative. The rule does not fire on `length(v)`, `dot(v, v)`, or other constructions that are mathematically guaranteed non-negative.

See the [What it detects](../rules/sqrt-of-potentially-negative.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sqrt-of-potentially-negative.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sqrt-of-potentially-negative.md -> Examples](../rules/sqrt-of-potentially-negative.md#examples).

## See also

- [Rule page](../rules/sqrt-of-potentially-negative.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
