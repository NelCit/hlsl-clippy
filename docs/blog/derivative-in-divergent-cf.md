---
title: "derivative-in-divergent-cf"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: derivative-in-divergent-cf
---

# derivative-in-divergent-cf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/derivative-in-divergent-cf.md](../rules/derivative-in-divergent-cf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The GPU computes screen-space derivatives by comparing register values across the 2x2 pixel quad that executes together. `ddx(v)` is the difference between the value of `v` in the left column and the right column of the quad; `ddy(v)` is the difference between the top row and the bottom row. For this subtraction to be meaningful, all four pixels in the quad must execute the same instruction at the same program counter simultaneously. When the branch condition is non-uniform, some lanes in the quad take the branch while others do not. The hardware still computes the derivative â€” by including the lanes that did not execute that instruction â€” but those lanes produce undefined or stale values. The result is silent data corruption: the mip level chosen by `Sample` will be wrong, gradients fed into `SampleGrad` will be garbage, and any subsequent computation on the derivative is undefined. No GPU exception is raised; the shader simply produces incorrect output.

## What the rule fires on

Calls to `ddx`, `ddy`, `ddx_coarse`, `ddy_coarse`, `ddx_fine`, `ddy_fine`, or texture sample intrinsics that use implicit screen-space derivatives (`Texture.Sample`, `Texture.SampleBias`) when they appear inside a branch whose condition depends on a non-uniform (per-pixel or per-lane varying) value. The rule fires when the condition expression is not provably dynamically uniform â€” i.e., it is not sourced exclusively from `cbuffer` fields, `nointerpolation` interpolants marked as uniform, or literal constants. Any `if`, `else if`, `else`, or ternary branch that contains such a sample or derivative call and whose predicate includes per-pixel data (interpolated values, SV_Position, texture reads, varying inputs) triggers the diagnostic.

See the [What it detects](../rules/derivative-in-divergent-cf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/derivative-in-divergent-cf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[derivative-in-divergent-cf.md -> Examples](../rules/derivative-in-divergent-cf.md#examples).

## See also

- [Rule page](../rules/derivative-in-divergent-cf.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
