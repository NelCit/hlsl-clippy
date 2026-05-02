---
title: "coopvec-fp4-fp6-blackwell-layout"
date: 2026-05-02
author: hlsl-clippy maintainers
category: blackwell
tags: [hlsl, performance, blackwell]
status: stub
related-rule: coopvec-fp4-fp6-blackwell-layout
---

# coopvec-fp4-fp6-blackwell-layout

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-fp4-fp6-blackwell-layout.md](../rules/coopvec-fp4-fp6-blackwell-layout.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

NVIDIA Blackwell 5th-gen Tensor Cores are FP4 / FP6-native; the optimal layout differs from Hopper FP8. Per the Blackwell Architecture v1.1 white paper and the arXiv 2512.02189 microbenchmarking paper, FP4/FP6 hits ~96.3% of theoretical peak only when the matrix is in `MATRIX_LAYOUT_INFERENCING_OPTIMAL` (or `TRAINING_OPTIMAL`); a row-major / column-major layout pays a swizzle cost on every fetch.

## What the rule fires on

A matrix matmul (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) with FP4 / FP6 element type (`COMPONENT_TYPE_FLOAT_E2M1`, `COMPONENT_TYPE_FLOAT_E3M2`, `COMPONENT_TYPE_FLOAT_E2M3`) using a non-OPTIMAL layout, under the `[experimental.target = blackwell]` config gate.

See the [What it detects](../rules/coopvec-fp4-fp6-blackwell-layout.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-fp4-fp6-blackwell-layout.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-fp4-fp6-blackwell-layout.md -> Examples](../rules/coopvec-fp4-fp6-blackwell-layout.md#examples).

## See also

- [Rule page](../rules/coopvec-fp4-fp6-blackwell-layout.md) -- canonical reference + change log.
- [blackwell overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*

**TODO:** category-overview missing for `blackwell`; linked overview is the closest sibling.