---
title: "redundant-computation-in-branch"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: redundant-computation-in-branch
---

# redundant-computation-in-branch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-computation-in-branch.md](../rules/redundant-computation-in-branch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

When a GPU compiler encounters a predicated `if`/`else` (the default for many short-arm branches on GPU hardware), both arms of the `if` execute unconditionally for every lane, with the results of the non-taken arm discarded by the write mask. In this execution model, a pure expression that appears in both arms executes twice â€” once in the `then` arm and once in the `else` arm â€” for every lane, regardless of which arm the lane actually takes. Hoisting the expression to before the `if` eliminates one of the two evaluations, reducing ALU cost by roughly half for that expression.

## What the rule fires on

An expression that appears identically in both the `then`-branch and the `else`-branch of the same `if`/`else` statement, where the expression is a pure computation (no texture samples with implicit derivatives, no writes to UAVs, no calls to intrinsics with side effects) and where all its operands are defined before the `if`. The rule fires when the syntactic form of the expression in both arms is identical and when the data-flow graph confirms that the operands resolve to the same values at the branch point â€” i.e., no operand is re-assigned between the start of the `if` and the use in either arm. It does not fire when the operands are different (even if the operator and literal constants are the same), when the expression involves a texture sample with implicit gradients (hoisting across a branch boundary would change the derivative context), or when the expression has observable side effects.

See the [What it detects](../rules/redundant-computation-in-branch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-computation-in-branch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-computation-in-branch.md -> Examples](../rules/redundant-computation-in-branch.md#examples).

## See also

- [Rule page](../rules/redundant-computation-in-branch.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
