---
title: "lerp-extremes"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: lerp-extremes
---

# lerp-extremes

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/lerp-extremes.md](../rules/lerp-extremes.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The `lerp(a, b, t)` intrinsic compiles to a multiply-add sequence: `a + t * (b - a)`, or on architectures with a native `lerp` instruction, to a single FMA-class operation. On AMD RDNA 3, `v_lerp_u8` exists for packed integer paths; for FP32 the compiler emits `v_fma_f32` (or two instructions: `v_sub_f32` + `v_mad_f32`). Either way, a call with a constant `t` of 0 or 1 performs arithmetic that reduces algebraically to a constant output â€” yet most GPU compilers do not eliminate the entire `lerp` call at the HLSL source level and instead rely on a later constant-folding pass that may not trigger across function boundaries or after inlining.

## What the rule fires on

Calls to `lerp(a, b, t)` where the interpolation weight `t` is a numeric literal equal to exactly 0 or 1 (including `0.0`, `1.0`, `0.0f`, `1.0f`). When `t` is 0, `lerp(a, b, 0)` is equivalent to `a`; when `t` is 1, `lerp(a, b, 1)` is equivalent to `b`. The rule fires on scalar, vector, and matrix overloads of `lerp`. It does not fire when `t` is a variable, a constant-buffer field, or a non-literal expression.

See the [What it detects](../rules/lerp-extremes.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/lerp-extremes.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[lerp-extremes.md -> Examples](../rules/lerp-extremes.md#examples).

## See also

- [Rule page](../rules/lerp-extremes.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
