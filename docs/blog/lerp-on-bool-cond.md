---
title: "lerp-on-bool-cond: Calls to `lerp(a, b, t)` where `t` is a value of `bool` (or vector-of-bool)…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: lerp-on-bool-cond
---

# lerp-on-bool-cond

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/lerp-on-bool-cond.md](../rules/lerp-on-bool-cond.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`lerp(a, b, t)` lowers in DXIL to an `FMad` of the form `a + (b - a) * t` — typically two FP32 operations: one subtract for `b - a` and one fused multiply-add. On AMD RDNA 2/3 that is one `v_sub_f32` plus one `v_fma_f32` issued at full VALU rate; on NVIDIA Turing/Ada it is one `FADD`/`FFMA` pair; on Intel Xe-HPG, the same shape on the EU's FMA pipeline. When `t` is a true floating-point fraction, this is exactly the right code. When `t` is a Boolean coerced to 0.0 or 1.0, the same two instructions execute but they degenerate into a select: `t == 0` returns `a`, `t == 1` returns `b`, and the multiply-add throws away one of the operands by multiplying it by zero.

## What the rule fires on

Calls to `lerp(a, b, t)` where `t` is a value of `bool` (or vector-of-bool) type that has been cast to a floating-point type at the call site — most commonly `lerp(a, b, (float)cond)` or `lerp(a, b, cond ? 1.0 : 0.0)`. The rule matches both the explicit C-style cast `(float)cond` and the construction-style `float(cond)`, and recognises the ternary-of-zero-and-one pattern as a syntactic fingerprint for the same intent. It does not fire on `lerp` calls whose third argument is a genuine continuous parameter (a UV coordinate, a fraction, a `saturate(...)` result), because that is the operation `lerp` exists to express.

See the [What it detects](../rules/lerp-on-bool-cond.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/lerp-on-bool-cond.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[lerp-on-bool-cond.md -> Examples](../rules/lerp-on-bool-cond.md#examples).

## See also

- [Rule page](../rules/lerp-on-bool-cond.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
