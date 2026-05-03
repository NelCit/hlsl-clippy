---
title: "compare-equal-float"
date: 2026-05-02
author: shader-clippy maintainers
category: misc
tags: [hlsl, performance, misc]
status: stub
related-rule: compare-equal-float
---

# compare-equal-float

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/compare-equal-float.md](../rules/compare-equal-float.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Floating-point numbers represent values with finite precision: a 32-bit `float` has a 23-bit mantissa, meaning the gap between adjacent representable values (the ULP, unit in the last place) grows with magnitude. Two computations that produce the "same" value by different instruction sequences â€” different order of operations, different FMA folding, different intermediate registers â€” can land in adjacent ULPs and compare unequal even when mathematically they should be identical.

## What the rule fires on

Any use of `==` or `!=` where both operands are of type `float`, `half`, `float2`/`float3`/`float4`, or the corresponding `half` vector types. The rule fires on the comparison operator node. It does not fire when either operand is of an integer type (`int`, `uint`, `bool`, etc.) or when the comparison is between a float value and a literal that the compiler can prove is exactly representable and the surrounding context is a known-safe pattern (such as comparing against `0.0` inside a `isnan`-style idiom â€” see Options). Integer `==` is correct and is never flagged.

See the [What it detects](../rules/compare-equal-float.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/compare-equal-float.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[compare-equal-float.md -> Examples](../rules/compare-equal-float.md#examples).

## See also

- [Rule page](../rules/compare-equal-float.md) -- canonical reference + change log.
- [misc overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*

**TODO:** category-overview missing for `misc`; linked overview is the closest sibling.