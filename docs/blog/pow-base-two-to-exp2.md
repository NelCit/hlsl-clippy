---
title: "pow-base-two-to-exp2"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: pow-base-two-to-exp2
---

# pow-base-two-to-exp2

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pow-base-two-to-exp2.md](../rules/pow-base-two-to-exp2.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`pow(2.0, x)` compiles to the full transcendental pair `exp2(x * log2(2.0))`, which simplifies algebraically to `exp2(x * 1.0)` â€” and then to `exp2(x)`. However, no shipping GPU shader compiler at the HLSL source level recognises this simplification and eliminates the `log2` call at compile time; the `log2(2.0)` is computed at runtime (or at most folded to a constant 1.0 in a separate constant-folding pass that may not combine with the surrounding `exp2`). On AMD RDNA 3, NVIDIA Ada Lovelace, and Intel Xe-HPG, the `pow` lowering path always emits both a `v_log_f32` and a `v_exp_f32` (or their DXIL equivalents), both of which run at one-quarter peak VALU throughput. The resulting instruction cost is the same as `pow(17.3, x)`.

## What the rule fires on

Calls to `pow(2.0, x)` â€” or `pow(2, x)` â€” where the base is a literal integer or floating-point constant equal to exactly 2.0. The rule fires when the first argument of `pow` is a numeric literal that evaluates to 2.0 after type coercion and the exponent `x` is any expression. It does not fire when the base is a variable, a constant-buffer field, or any non-literal expression.

See the [What it detects](../rules/pow-base-two-to-exp2.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pow-base-two-to-exp2.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pow-base-two-to-exp2.md -> Examples](../rules/pow-base-two-to-exp2.md#examples).

## See also

- [Rule page](../rules/pow-base-two-to-exp2.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
