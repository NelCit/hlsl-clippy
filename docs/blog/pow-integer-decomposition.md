---
title: "pow-integer-decomposition: Calls to `pow(x, e)` where the exponent `e` is a literal integer or floating-point…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: pow-integer-decomposition
---

# pow-integer-decomposition

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pow-integer-decomposition.md](../rules/pow-integer-decomposition.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

As described in [pow-to-mul](pow-to-mul.md), `pow(x, e)` on AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG always lowers to `exp2(e * log2(x))` regardless of the exponent value. A `pow(x, 5.0)` call carries the same two-quarter-rate-instruction cost as `pow(x, 5000.0)`. For small integer exponents that cost can be replaced by a sequence of full-rate multiply instructions using the pow-by-squaring algorithm: `pow(x, 5)` becomes `x2 = x*x; x4 = x2*x2; return x4*x;` — three multiplies, all at full VALU rate. On RDNA 3, a full-rate FP32 multiply issues at 1 per clock per SIMD32 lane; three of them cost 3 full-rate cycles versus the ~4 effective cycles of the transcendental pair, and critically they occupy the general VALU rather than the shared transcendental unit (TALU).

## What the rule fires on

Calls to `pow(x, e)` where the exponent `e` is a literal integer or floating-point constant equal to an integer value of 5 or greater (up to a configurable ceiling, default 16). The rule fires when the second argument is a numeric literal that evaluates to a whole number in that range. It does not fire when the exponent is a variable, a constant-buffer field, or a non-integer-valued literal. Exponents 2.0, 3.0, and 4.0 are handled by [pow-to-mul](pow-to-mul.md); exponents exceeding the ceiling (default 16) are not flagged because the mul-chain becomes longer than the transcendental cost.

See the [What it detects](../rules/pow-integer-decomposition.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pow-integer-decomposition.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pow-integer-decomposition.md -> Examples](../rules/pow-integer-decomposition.md#examples).

## See also

- [Rule page](../rules/pow-integer-decomposition.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
