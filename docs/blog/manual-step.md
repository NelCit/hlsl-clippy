---
title: "manual-step"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-step
---

# manual-step

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-step.md](../rules/manual-step.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

In GLSL/HLSL shader code, conditional expressions `x > a ? 1.0 : 0.0` are common in material shaders for binary masks, alpha cutouts, and clipping thresholds. On a GPU, a ternary conditional in a shader may compile to a comparison instruction plus a `select`/`conditional move` (e.g., `v_cndmask_b32` on RDNA, `SEL` on Xe-HPG). This is already efficient â€” there is no branch divergence because the compiler recognises the constant-branch ternary â€” but the pattern still requires a comparison instruction that produces a boolean predicate and a separate select instruction that consumes it.

## What the rule fires on

A ternary conditional expression of the form `x > a ? 1.0 : 0.0` (or the equivalent `x >= a ? 1.0 : 0.0`, `a < x ? 1.0 : 0.0`, `a <= x ? 1.0 : 0.0`) where the true-branch is the literal 1 and the false-branch is the literal 0. The rule also matches integer forms `x > a ? 1 : 0`. It does not fire when the two branches are not the literal constants 0 and 1, or when the comparison direction does not match the `step` semantics (`step(a, x)` returns 1 when `x >= a`). In HLSL, `step(a, x)` returns `0.0` if `x < a` and `1.0` if `x >= a`.

See the [What it detects](../rules/manual-step.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-step.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-step.md -> Examples](../rules/manual-step.md#examples).

## See also

- [Rule page](../rules/manual-step.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
