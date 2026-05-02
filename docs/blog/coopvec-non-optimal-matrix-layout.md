---
title: "coopvec-non-optimal-matrix-layout"
date: 2026-05-02
author: hlsl-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-non-optimal-matrix-layout
---

# coopvec-non-optimal-matrix-layout

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-non-optimal-matrix-layout.md](../rules/coopvec-non-optimal-matrix-layout.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Cooperative Vectors (SM 6.9) target the tensor-core / matrix-engine hardware on each IHV: NVIDIA Ada Lovelace's tensor cores, AMD RDNA 3/4's WMMA path, and Intel Xe-HPG's XMX engines. Each engine prefers a vendor-specific weight layout so the matrix-element fetch hits the engine's native swizzle pattern in a single transaction. The HLSL spec exposes two opaque enums â€” `MATRIX_LAYOUT_INFERENCING_OPTIMAL` and `MATRIX_LAYOUT_TRAINING_OPTIMAL` â€” that the driver maps to its hardware-preferred layout at upload time via the `D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_CONVERT` API.

## What the rule fires on

A `dx::linalg::MatrixMul`, `dx::linalg::MatrixVectorMul`, or `dx::linalg::OuterProductAccumulate` call whose matrix-layout enum argument is not one of the IHV-optimal layouts (`MATRIX_LAYOUT_INFERENCING_OPTIMAL` for the inference path, `MATRIX_LAYOUT_TRAINING_OPTIMAL` for the training path). The rule walks the matrix-handle constant-fold chain, identifies the layout enum at the call site, and fires when a row-major or column-major layout is used for an inference matrix.

See the [What it detects](../rules/coopvec-non-optimal-matrix-layout.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-non-optimal-matrix-layout.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-non-optimal-matrix-layout.md -> Examples](../rules/coopvec-non-optimal-matrix-layout.md#examples).

## See also

- [Rule page](../rules/coopvec-non-optimal-matrix-layout.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
