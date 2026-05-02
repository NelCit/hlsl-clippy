---
id: coopvec-fp4-fp6-blackwell-layout
category: blackwell
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# coopvec-fp4-fp6-blackwell-layout

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A matrix matmul (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`)
with FP4 / FP6 element type (`COMPONENT_TYPE_FLOAT_E2M1`,
`COMPONENT_TYPE_FLOAT_E3M2`, `COMPONENT_TYPE_FLOAT_E2M3`) using a non-OPTIMAL
layout, under the `[experimental.target = blackwell]` config gate.

## Why it matters on a GPU

NVIDIA Blackwell 5th-gen Tensor Cores are FP4 / FP6-native; the optimal
layout differs from Hopper FP8. Per the Blackwell Architecture v1.1 white
paper and the arXiv 2512.02189 microbenchmarking paper, FP4/FP6 hits
~96.3% of theoretical peak only when the matrix is in
`MATRIX_LAYOUT_INFERENCING_OPTIMAL` (or `TRAINING_OPTIMAL`); a row-major /
column-major layout pays a swizzle cost on every fetch.

## Options

none. Activated only under `[experimental] target = "blackwell"`.

## Fix availability

**suggestion** — Switch to `MATRIX_LAYOUT_INFERENCING_OPTIMAL` /
`TRAINING_OPTIMAL`.

## See also

- NVIDIA Blackwell Architecture v1.1 PDF
- arXiv 2512.02189: Microbenchmarking NVIDIA's Blackwell Architecture
