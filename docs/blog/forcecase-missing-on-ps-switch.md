---
title: "forcecase-missing-on-ps-switch"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: forcecase-missing-on-ps-switch
---

# forcecase-missing-on-ps-switch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/forcecase-missing-on-ps-switch.md](../rules/forcecase-missing-on-ps-switch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The HLSL `switch` statement has multiple valid lowerings: a true jump-table (one indirect branch, one taken target per lane), a chained `if`/`else` ladder (linear scan over case labels), or â€” in some compiler versions â€” a branch-free predicate fan that evaluates every case body and selects the right result. The `[forcecase]` attribute pins the compiler to the jump-table form. Without `[forcecase]`, the compiler is free to pick the chained-`if` form, and that's where the pixel-shader hazard appears: chained `if`s mean each case introduces a new branch, and per-pixel divergence on case selection retires quad lanes one-arm-at-a-time. Derivatives sampled inside the case body then see undefined neighbour values from retired lanes â€” exactly the same failure mode as a per-lane `if` containing a `Sample` (see [derivative-in-divergent-cf](derivative-in-divergent-cf.md) and [quadany-quadall-opportunity](quadany-quadall-opportunity.md)).

## What the rule fires on

A `switch` statement inside a pixel-shader entry point where at least one case body contains a derivative-bearing operation â€” `Sample`, `SampleBias`, `SampleGrad`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, or any function call that transitively invokes one â€” and the `switch` lacks the `[forcecase]` attribute. The rule fires only on PS entry points (the hazard is specific to the quad / derivative model); compute and other stages are not flagged.

See the [What it detects](../rules/forcecase-missing-on-ps-switch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/forcecase-missing-on-ps-switch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[forcecase-missing-on-ps-switch.md -> Examples](../rules/forcecase-missing-on-ps-switch.md#examples).

## See also

- [Rule page](../rules/forcecase-missing-on-ps-switch.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
