---
title: "comparison-with-nan-literal: Any comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`) whose left or right operand is…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: misc
tags: [hlsl, performance, misc]
status: stub
related-rule: comparison-with-nan-literal
---

# comparison-with-nan-literal

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/comparison-with-nan-literal.md](../rules/comparison-with-nan-literal.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

IEEE 754 mandates that every ordered comparison involving a NaN operand returns `false`, and every unordered comparison returns `true`. This means `x < (0.0 / 0.0)` is unconditionally `false` for all finite, infinite, and NaN values of `x` — including when `x` is itself NaN. The comparison is a constant, not a runtime test.

## What the rule fires on

Any comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`) whose left or right operand is a literal NaN-producing expression. The canonical forms are `0.0 / 0.0` (zero divided by zero), `sqrt(-1)` or `sqrt(-1.0)` (square root of a negative literal), and any constant-foldable expression that the front-end can statically resolve to a NaN bit pattern. The rule fires on the comparison node, not on the NaN-producing sub-expression in isolation.

See the [What it detects](../rules/comparison-with-nan-literal.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/comparison-with-nan-literal.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[comparison-with-nan-literal.md -> Examples](../rules/comparison-with-nan-literal.md#examples).

## See also

- [Rule page](../rules/comparison-with-nan-literal.md) -- canonical reference + change log.
- [misc overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*

**TODO:** category-overview missing for `misc`; linked overview is the closest sibling.