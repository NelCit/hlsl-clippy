---
title: "length-then-divide"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: length-then-divide
---

# length-then-divide

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/length-then-divide.md](../rules/length-then-divide.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`normalize(v)` is an HLSL intrinsic that lowers to the canonical `v * rsqrt(dot(v, v))` sequence on every shipping GPU compiler. The manual `v / length(v)` lowers to `v / sqrt(dot(v, v))`, which is materially worse on the hardware. On AMD RDNA 3, `v_sqrt_f32` and `v_rsq_f32` both occupy the transcendental unit at one-quarter VALU throughput, so the square-root step is the same cost. The difference is the divide: GPU FP division is not a single-cycle operation. RDNA 3 implements scalar FP32 divide as a software macro built on `v_rcp_f32` (one transcendental issue) plus a Newton-Raphson refinement step, totalling roughly 3-5 effective cycles. NVIDIA Ada Lovelace lowers FP32 divide similarly, with `MUFU.RCP` plus refinement on the multi-function unit. The `rsqrt`-and-multiply path replaces the divide with one rcp-equivalent transcendental and one full-rate multiply per vector component â€” a net saving of 2-4 cycles per normalisation site over the divide form, before counting the vector multiply that the divide must do per-component anyway.

## What the rule fires on

Expressions that normalise a vector by manually dividing it by its own length, in any of these forms: the inline `v / length(v)`, the temporary form `float l = length(v); ... v / l;`, the multiply-by-reciprocal form `v * (1.0 / length(v))`, and the rsqrt-of-dot form `v * rsqrt(dot(v, v))` where the `dot` argument matches the multiplied vector. The rule keys on a vector expression that is either divided by `length()` of the same vector, or multiplied by the reciprocal or `rsqrt`-of-self-dot of the same vector. It does not fire when the divisor is a length of a different vector (`v / length(w)` is a legitimate scaling operation), nor when the vector is divided by a stored magnitude that may have been computed earlier and is reused.

See the [What it detects](../rules/length-then-divide.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/length-then-divide.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[length-then-divide.md -> Examples](../rules/length-then-divide.md#examples).

## See also

- [Rule page](../rules/length-then-divide.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
