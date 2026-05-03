---
title: "length-comparison"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: length-comparison
---

# length-comparison

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/length-comparison.md](../rules/length-comparison.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`length(v)` computes `sqrt(dot(v, v))`. On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sqrt` is a quarter-rate transcendental instruction (`v_sqrt_f32` on RDNA). Comparing `length(v) < r` introduces a `sqrt` whose only purpose is to produce a value that is immediately compared against `r`. Since `sqrt` is monotonically increasing for non-negative inputs, the comparison `length(v) < r` is equivalent to `dot(v, v) < r * r` for any `r >= 0`. The squared form eliminates the `sqrt` entirely, replacing a quarter-rate transcendental with two full-rate FP32 multiply-adds.

## What the rule fires on

A relational comparison of the form `length(v) < r`, `length(v) > r`, `length(v) <= r`, or `length(v) >= r` where `r` is any scalar expression. The rule also matches `distance(a, b) < r` and related forms (which lower to `length(a - b) < r`). It does not fire when `length` appears in an arithmetic expression rather than a direct comparison, or when the comparison involves expressions that are not a single scalar radius (e.g., `length(v) < f(x)` is still matched, but requires the radius expression to be trivially non-negative for the squared form to be safe; the tool emits a note when it cannot verify this).

See the [What it detects](../rules/length-comparison.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/length-comparison.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[length-comparison.md -> Examples](../rules/length-comparison.md#examples).

## See also

- [Rule page](../rules/length-comparison.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
