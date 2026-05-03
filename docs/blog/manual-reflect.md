---
title: "manual-reflect"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-reflect
---

# manual-reflect

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-reflect.md](../rules/manual-reflect.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The manual expression `v - 2.0 * dot(n, v) * n` decomposes into a `dot` (one FP32 multiply-accumulate per component pair, reducing to a scalar), a scalar multiply by 2.0, a vector-scalar multiply (one multiply per component), and a vector subtract (one subtract per component). For `float3` inputs that is approximately 3 + 1 + 3 + 3 = 10 FP32 operations issued as individual VALU instructions. Depending on MAD folding, a compiler may reduce some of those, but across function boundaries or in inlined code the pattern often remains unoptimised.

## What the rule fires on

The expression `v - 2.0 * dot(n, v) * n` (or algebraically equivalent forms such as `v - 2 * dot(n, v) * n` or `v - dot(n, v) * n * 2.0`) where `v` and `n` are vector expressions. This is the standard reflection formula for reflecting incident vector `v` about a surface normal `n`. The rule matches the structural pattern of a subtract-two-dot-product-scale combination, allowing for commutativity of the scalar factors and the dot-product argument order. It does not fire on partial forms (e.g., only part of the formula) or when the factor is not 2.

See the [What it detects](../rules/manual-reflect.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-reflect.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-reflect.md -> Examples](../rules/manual-reflect.md#examples).

## See also

- [Rule page](../rules/manual-reflect.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
