---
title: "manual-mad-decomposition: A multiply followed by an add that the author has split across two statements…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-mad-decomposition
---

# manual-mad-decomposition

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-mad-decomposition.md](../rules/manual-mad-decomposition.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Fused multiply-add (FMA) is the fundamental arithmetic primitive of every modern GPU. AMD RDNA 3 issues `v_fma_f32` and `v_fmac_f32` as full-rate VALU instructions — one per SIMD32 lane per clock. NVIDIA Turing, Ampere, and Ada Lovelace issue `FFMA` at full FP32 throughput; on Ada the FFMA path is the largest single contributor to the advertised FP32 TFLOPS figure. Intel Xe-HPG `mad` on the EU vector pipe is similarly first-class. When the compiler can see a multiply-then-add chain, it folds the pair into one instruction: half the issue slots, half the register lifetime for the intermediate, and one rounding step instead of two (FMA is more accurate than separate mul + add by IEEE 754-2008 definition).

## What the rule fires on

A multiply followed by an add that the author has split across two statements with a named temporary, in the form `T t = a * b; ... U u = t + c;` where `t` has no other use between its definition and the add. The rule also matches the symmetric `a * b` plus `c` pattern when `a * b` is computed in one statement and added to `c` in a non-adjacent statement, or when the result of the multiply is stored into a struct field or `out` parameter that is then read once into the add. The detection is structural: a multiply expression whose result feeds exactly one add expression downstream, with the two expressions separated by enough statements that the optimiser may not see them as a single basic-block fold candidate. It does not fire when the temporary is genuinely reused (referenced more than once) or when the intermediate is exported across a function boundary.

See the [What it detects](../rules/manual-mad-decomposition.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-mad-decomposition.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-mad-decomposition.md -> Examples](../rules/manual-mad-decomposition.md#examples).

## See also

- [Rule page](../rules/manual-mad-decomposition.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
