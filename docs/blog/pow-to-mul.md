---
title: "pow-to-mul"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: pow-to-mul
---

# pow-to-mul

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pow-to-mul.md](../rules/pow-to-mul.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`pow(x, e)` is not compiled to repeated multiplication by any shipping GPU compiler. On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, the `pow` intrinsic lowers to the transcendental pair `exp2(e * log2(x))`. Each of `v_log_f32` and `v_exp_f32` on RDNA 3 executes at one-quarter the peak VALU throughput â€” so a single `pow(x, 3.0)` occupies the equivalent of roughly 4 full-rate ALU cycles, the same cost as `pow(x, 37.5)`. The exponent value 3.0 does not make the instruction cheaper.

## What the rule fires on

Calls to `pow(x, e)` where the exponent `e` is a literal integer or floating-point constant equal to 2.0, 3.0, or 4.0. The rule fires on any expression whose second argument is a numeric literal that evaluates to exactly 2.0, 3.0, or 4.0 after type coercion. It does not fire when the exponent is a variable, a constant-buffer field, or any non-literal expression. The squared case (`pow(x, 2.0)`) is also covered in detail by [pow-const-squared](pow-const-squared.md); this rule extends coverage to the 3.0 and 4.0 cases and serves as the canonical entry point for the broader mul-expansion family.

See the [What it detects](../rules/pow-to-mul.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pow-to-mul.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pow-to-mul.md -> Examples](../rules/pow-to-mul.md#examples).

## See also

- [Rule page](../rules/pow-to-mul.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
