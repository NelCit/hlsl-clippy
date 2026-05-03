---
title: "inv-sqrt-to-rsqrt"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: inv-sqrt-to-rsqrt
---

# inv-sqrt-to-rsqrt

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/inv-sqrt-to-rsqrt.md](../rules/inv-sqrt-to-rsqrt.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sqrt` is a transcendental instruction (`v_sqrt_f32` on RDNA, `SQRT` on Xe-HPG) executing at one-quarter peak VALU throughput. A reciprocal (`rcp`, `v_rcp_f32`) is also a special-function-unit instruction, again at quarter rate. The expression `1.0 / sqrt(x)` therefore chains two quarter-rate instructions â€” sqrt, then rcp â€” for an effective cost of roughly 8 full-rate cycles per wave on most architectures.

## What the rule fires on

The expression `1.0 / sqrt(x)` â€” or `1.0f / sqrt(x)`, `rcp(sqrt(x))`, or a division by `sqrt` of any sub-expression â€” where the numerator is the literal constant 1. The rule matches the structural pattern of a reciprocal of a square root, regardless of whether `x` is a scalar, vector component, or a compound expression such as `dot(v, v)`. It does not fire when the numerator is not a constant 1, or when the division is by something other than `sqrt(...)`.

See the [What it detects](../rules/inv-sqrt-to-rsqrt.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/inv-sqrt-to-rsqrt.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[inv-sqrt-to-rsqrt.md -> Examples](../rules/inv-sqrt-to-rsqrt.md#examples).

## See also

- [Rule page](../rules/inv-sqrt-to-rsqrt.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
