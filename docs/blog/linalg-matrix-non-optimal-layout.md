---
title: "linalg-matrix-non-optimal-layout: A `linalg::*Mul` / `OuterProductAccumulate` call (SM 6.10, proposal 0035) whose matrix-layout enum argument is…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: linalg
tags: [hlsl, performance, linalg]
status: stub
related-rule: linalg-matrix-non-optimal-layout
---

# linalg-matrix-non-optimal-layout

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/linalg-matrix-non-optimal-layout.md](../rules/linalg-matrix-non-optimal-layout.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

SM 6.10 promotes `vector::CooperativeVector` to `linalg::Matrix`. Drivers route `OPTIMAL`-tagged matrices to the on-chip matrix engine without a per- element swizzle; the matrix-engine fetcher (NVIDIA Blackwell 5th-gen Tensor Cores for FP4/FP6, NVIDIA Hopper for FP8, AMD RDNA 4's 2nd-gen AI accelerator, Intel Xe2 XMX) is gated on the OPTIMAL layout. A row-major / column-major declaration triggers a per-element swizzle on every fetch, costing 2-4x throughput on every IHV's matrix engine.

## What the rule fires on

A `linalg::*Mul` / `OuterProductAccumulate` call (SM 6.10, proposal 0035) whose matrix-layout enum argument is `MATRIX_LAYOUT_ROW_MAJOR` or `MATRIX_LAYOUT_COLUMN_MAJOR` instead of the IHV-preferred `MATRIX_LAYOUT_INFERENCING_OPTIMAL` / `MATRIX_LAYOUT_TRAINING_OPTIMAL`. The rule activates only on SM 6.10+ targets and is the SM 6.10 successor to `coopvec-non-optimal-matrix-layout`.

See the [What it detects](../rules/linalg-matrix-non-optimal-layout.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/linalg-matrix-non-optimal-layout.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[linalg-matrix-non-optimal-layout.md -> Examples](../rules/linalg-matrix-non-optimal-layout.md#examples).

## See also

- [Rule page](../rules/linalg-matrix-non-optimal-layout.md) -- canonical reference + change log.
- [linalg overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
