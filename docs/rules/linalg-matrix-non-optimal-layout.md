---
id: linalg-matrix-non-optimal-layout
category: linalg
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# linalg-matrix-non-optimal-layout

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `linalg::*Mul` / `OuterProductAccumulate` call (SM 6.10, proposal 0035) whose
matrix-layout enum argument is `MATRIX_LAYOUT_ROW_MAJOR` or
`MATRIX_LAYOUT_COLUMN_MAJOR` instead of the IHV-preferred
`MATRIX_LAYOUT_INFERENCING_OPTIMAL` / `MATRIX_LAYOUT_TRAINING_OPTIMAL`.
The rule activates only on SM 6.10+ targets and is the SM 6.10 successor to
`coopvec-non-optimal-matrix-layout`.

## Why it matters on a GPU

SM 6.10 promotes `vector::CooperativeVector` to `linalg::Matrix`. Drivers
route `OPTIMAL`-tagged matrices to the on-chip matrix engine without a per-
element swizzle; the matrix-engine fetcher (NVIDIA Blackwell 5th-gen Tensor
Cores for FP4/FP6, NVIDIA Hopper for FP8, AMD RDNA 4's 2nd-gen AI
accelerator, Intel Xe2 XMX) is gated on the OPTIMAL layout. A row-major /
column-major declaration triggers a per-element swizzle on every fetch,
costing 2-4x throughput on every IHV's matrix engine.

## Examples

### Bad

```hlsl
linalg::MatrixMul<linalg::Matrix<float, 4, 4>>(MATRIX_LAYOUT_ROW_MAJOR);
```

### Good

```hlsl
linalg::MatrixMul<linalg::Matrix<float, 4, 4>>(MATRIX_LAYOUT_INFERENCING_OPTIMAL);
```

## Options

none

## Fix availability

**suggestion** — The OPTIMAL layout requires the developer to choose between
inferencing and training optima.

## See also

- Related rule: [coopvec-non-optimal-matrix-layout](coopvec-non-optimal-matrix-layout.md)
- HLSL Specs proposal 0035: `linalg::Matrix`
- Companion blog post: _not yet published_
