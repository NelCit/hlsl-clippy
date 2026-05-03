---
title: "precise-missing-on-iterative-refine"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: precise-missing-on-iterative-refine
---

# precise-missing-on-iterative-refine

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/precise-missing-on-iterative-refine.md](../rules/precise-missing-on-iterative-refine.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU compilers for HLSL run aggressive fast-math reordering by default. The optimiser sees `x = x - f(x)/fprime(x)` and is free to: (a) apply mul-add fusion that changes rounding, (b) reorder the subtraction with respect to constant folds, (c) re-associate `x*x*x` to `x*(x*x)` or `pow(x, 3)`, and crucially (d) recognise that across an unbounded number of iterations the algebraic limit collapses certain residuals to zero â€” at which point the optimiser may fold the entire iteration to the initial guess, on the grounds that `x = x - 0` is identity. On AMD RDNA 2/3, NVIDIA Ada Lovelace, and Intel Xe-HPG the resulting codegen is silently a no-op iteration: the initial guess passes through unchanged. The author intended N rounds of quadratic-convergence refinement; the shader runs zero rounds.

## What the rule fires on

A loop whose body implements a Newton-Raphson, Halley, or related iterative-refinement step on a floating-point quantity, where the residual update (the term that drives convergence) lacks the `precise` qualifier. Pattern shapes: `for (uint i = 0; i < N; ++i) { x = x - f(x) / fprime(x); }` or the equivalent `x = (x + a / x) * 0.5;` for square root, or `x = x * (1.5 - 0.5 * a * x * x);` for inverse-square-root refinement, where `x` is a plain `float` rather than `precise float`.

See the [What it detects](../rules/precise-missing-on-iterative-refine.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/precise-missing-on-iterative-refine.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[precise-missing-on-iterative-refine.md -> Examples](../rules/precise-missing-on-iterative-refine.md#examples).

## See also

- [Rule page](../rules/precise-missing-on-iterative-refine.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
