---
title: "small-loop-no-unroll"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: small-loop-no-unroll
---

# small-loop-no-unroll

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/small-loop-no-unroll.md](../rules/small-loop-no-unroll.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A GPU loop compiled without `[unroll]` generates real branch instructions: a counter decrement, a compare, and a conditional backward branch at the bottom of each iteration. On a wave of 32 or 64 lanes, this overhead is paid once per iteration but amortises across all lanes simultaneously â€” the branch is uniform, so there is no divergence penalty, and the branch predictor (on hardware that has one) can predict it with high accuracy for small-count loops. However, the overhead is still non-zero: the counter update and compare consume ALU cycles, the backward edge occupies fetch bandwidth, and the loop carries a data dependency on the counter variable that can limit instruction-level parallelism within the loop body.

## What the rule fires on

`for` or `while` loops whose trip count is a compile-time constant (a literal integer, a `static const` expression, or an expression that reduces to a constant at compile time) and whose trip count is at or below the configured threshold (`max-iterations`, default 8), when the loop does not carry a `[unroll]` or `[unroll(N)]` attribute. The rule fires on loops whose bounds are fully determined at parse time; it does not fire on loops whose trip count depends on a cbuffer field, a function parameter, or any non-constant expression, even if that expression always evaluates to a small value at runtime.

See the [What it detects](../rules/small-loop-no-unroll.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/small-loop-no-unroll.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[small-loop-no-unroll.md -> Examples](../rules/small-loop-no-unroll.md#examples).

## See also

- [Rule page](../rules/small-loop-no-unroll.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
