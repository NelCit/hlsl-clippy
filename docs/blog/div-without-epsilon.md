---
title: "div-without-epsilon: Division expressions `a / b` where the divisor `b` is one of: a `length(...)`…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: div-without-epsilon
---

# div-without-epsilon

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/div-without-epsilon.md](../rules/div-without-epsilon.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

IEEE 754 single-precision division by zero produces `+inf` (positive numerator) or `-inf` (negative numerator), and `0.0 / 0.0` produces NaN. Both classes of result corrupt the shader's downstream math: `inf - inf` is NaN, `inf * 0` is NaN, any comparison with NaN returns false (so an `if (x > 0)` guard does not catch a NaN from a previous step). The hardware does not flag the operation; on AMD RDNA 2/3, `v_div_f32` is implemented as `v_rcp_f32` followed by `v_mul_f32`, both of which produce IEEE-conforming inf/NaN on degenerate inputs. NVIDIA Turing / Ada use the multi-function unit's `MUFU.RCP` with the same behaviour. Intel Xe-HPG's transcendental pipe is identical.

## What the rule fires on

Division expressions `a / b` where the divisor `b` is one of: a `length(...)` of an arbitrary vector, a `dot(v, v)` (squared length), a `dot(a, b)` between two vectors that may be perpendicular, the difference of two values that may be equal (`x - y` used as a divisor), or any expression that the rule can statically prove may evaluate to zero on plausible inputs. The same pattern applies to `rcp(b)` and to the implicit reciprocal in `pow(x, -n)` style code.

See the [What it detects](../rules/div-without-epsilon.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/div-without-epsilon.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[div-without-epsilon.md -> Examples](../rules/div-without-epsilon.md#examples).

## See also

- [Rule page](../rules/div-without-epsilon.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
