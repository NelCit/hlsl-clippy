---
title: "select-vs-lerp-of-constant: Calls to `lerp(K1, K2, t)` where both `K1` and `K2` are compile-time constant scalars…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: select-vs-lerp-of-constant
---

# select-vs-lerp-of-constant

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/select-vs-lerp-of-constant.md](../rules/select-vs-lerp-of-constant.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`lerp(K1, K2, t)` is mathematically `K1 + (K2 - K1) * t`. With both `K1` and `K2` known at compile time, `K2 - K1` is itself a constant — call it `D` — and the entire expression collapses to a single fused multiply-add: `mad(t, D, K1)`. On AMD RDNA 2/3 that is one `v_fma_f32` issued at full VALU rate; on NVIDIA Turing and Ada Lovelace, one `FFMA`; on Intel Xe-HPG, one FMA on the EU. When the compiler sees the constant operands and folds `K2 - K1` ahead of time, the call costs one ALU cycle. When the compiler does *not* fold — and this is where the rule earns its keep — the call costs two: one to compute `K2 - K1` at runtime, one for the FMA, plus an extra register to hold the temporary.

## What the rule fires on

Calls to `lerp(K1, K2, t)` where both `K1` and `K2` are compile-time constant scalars or constant vector literals, and `t` is a runtime value. The constants may appear as numeric literals (`0.5`, `float3(1, 0, 0)`), as named `static const` declarations whose initialiser is itself constant, or as expressions that fold to constants at parse time (`1.0 / 3.0`, `2.0 * PI`). The rule does not fire when either endpoint is a runtime expression — that case is the operation `lerp` exists to express. It also does not fire when both endpoints *and* `t` are constants, because that is dead code the compiler folds outright (and a separate constant-folding rule is the right home for it).

See the [What it detects](../rules/select-vs-lerp-of-constant.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/select-vs-lerp-of-constant.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[select-vs-lerp-of-constant.md -> Examples](../rules/select-vs-lerp-of-constant.md#examples).

## See also

- [Rule page](../rules/select-vs-lerp-of-constant.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
