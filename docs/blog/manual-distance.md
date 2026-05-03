---
title: "manual-distance"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-distance
---

# manual-distance

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-distance.md](../rules/manual-distance.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`length(v)` computes `sqrt(dot(v, v))`. When given `a - b` as its argument, the sub-expression `a - b` is computed first (one subtraction per component), then passed to `sqrt(dot(..., ...))`. This involves: a vector subtract (3 ops for `float3`), a dot product (3 multiplies + 2 adds = 5 ops), and a `sqrt` at quarter-rate TALU throughput. The total for `float3` is roughly 8 full-rate ops plus one quarter-rate `sqrt`.

## What the rule fires on

The expression `length(a - b)` where `a` and `b` are vector expressions. The rule matches a call to the `length` intrinsic whose sole argument is a subtraction of two vector operands. It applies to all vector types (`float2`, `float3`, `float4`, and their typed equivalents). It does not fire when the argument to `length` is not a binary subtraction, or when the subtraction is part of a larger expression before being passed to `length`.

See the [What it detects](../rules/manual-distance.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-distance.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-distance.md -> Examples](../rules/manual-distance.md#examples).

## See also

- [Rule page](../rules/manual-distance.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
