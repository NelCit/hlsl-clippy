---
title: "long-vector-non-elementwise-intrinsic"
date: 2026-05-02
author: shader-clippy maintainers
category: long-vectors
tags: [hlsl, performance, long-vectors]
status: stub
related-rule: long-vector-non-elementwise-intrinsic
---

# long-vector-non-elementwise-intrinsic

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/long-vector-non-elementwise-intrinsic.md](../rules/long-vector-non-elementwise-intrinsic.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The SM 6.9 long-vector feature is a codegen change: DXIL gains first-class `vector<T, N>` types so the compiler can emit one DXIL operation that the IHV scalarizer expands into the natural wave shape of the target GPU. NVIDIA Ada Lovelace's compiler emits packed-math FP16 sequences for `vector<float16_t, 16>`; AMD RDNA 3's compiler maps `vector<float, 8>` onto the wave-wide register file with one VALU lane per element pair; Intel Xe-HPG's compiler uses the SIMD16/SIMD32 hardware width. The point of the feature is to skip the manual array-of-vec4 unrolling that pre-SM-6.9 code wrote.

## What the rule fires on

A call to a non-elementwise HLSL intrinsic â€” `cross`, `length`, `normalize`, `dot` (under specific arity rules), `transpose`, `mul` against a matrix, `determinant` â€” applied to a `vector<T, N>` with `N >= 5`. The SM 6.9 long-vector specification (DXIL vectors, proposal 0030) extends `vector<T, N>` to support `5 <= N <= 1024`, but only for *elementwise* intrinsics: arithmetic operators, `abs`, `sqrt`, `exp`, `log`, `min`, `max`, `mad`, `lerp`, `step`, `saturate`, comparison operators, and the like. Geometric, structural, and matrix-shape intrinsics are explicitly out of scope and produce a hard DXC validation error. The rule is pure AST: intrinsic name plus the literal vector type on the argument.

See the [What it detects](../rules/long-vector-non-elementwise-intrinsic.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/long-vector-non-elementwise-intrinsic.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[long-vector-non-elementwise-intrinsic.md -> Examples](../rules/long-vector-non-elementwise-intrinsic.md#examples).

## See also

- [Rule page](../rules/long-vector-non-elementwise-intrinsic.md) -- canonical reference + change log.
- [long-vectors overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
