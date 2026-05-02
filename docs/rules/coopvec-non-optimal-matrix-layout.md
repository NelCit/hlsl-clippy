---
id: coopvec-non-optimal-matrix-layout
category: cooperative-vector
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# coopvec-non-optimal-matrix-layout

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::linalg::MatrixMul`, `dx::linalg::MatrixVectorMul`, or `dx::linalg::OuterProductAccumulate` call whose matrix-layout enum argument is not one of the IHV-optimal layouts (`MATRIX_LAYOUT_INFERENCING_OPTIMAL` for the inference path, `MATRIX_LAYOUT_TRAINING_OPTIMAL` for the training path). The rule walks the matrix-handle constant-fold chain, identifies the layout enum at the call site, and fires when a row-major or column-major layout is used for an inference matrix.

## Why it matters on a GPU

Cooperative Vectors (SM 6.9) target the tensor-core / matrix-engine hardware on each IHV: NVIDIA Ada Lovelace's tensor cores, AMD RDNA 3/4's WMMA path, and Intel Xe-HPG's XMX engines. Each engine prefers a vendor-specific weight layout so the matrix-element fetch hits the engine's native swizzle pattern in a single transaction. The HLSL spec exposes two opaque enums — `MATRIX_LAYOUT_INFERENCING_OPTIMAL` and `MATRIX_LAYOUT_TRAINING_OPTIMAL` — that the driver maps to its hardware-preferred layout at upload time via the `D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_CONVERT` API.

Using a generic row-major or column-major layout works (the operation is well-defined), but the matrix engine has to perform a per-element transpose / swizzle on every fetch. NVIDIA's published cooperative-vector blog cites speedups of 2-4x on inference-tier matmuls when the optimal layout is used vs. the generic row-major path; AMD and Intel's RDNA 3 / Xe-HPG WMMA documentation cite similar magnitudes. The cost of the conversion is paid once at upload time, not per dispatch — so the optimal path wins decisively for any matrix used more than once.

The fix is to convert the matrix at upload time using `ID3D12LinearAlgebra` and switch the call-site enum. The rule is suggestion-tier because the conversion has to happen application-side; the diagnostic names the call site and the current layout so the developer can add the conversion to their asset-loading path.

## Examples

### Bad

```hlsl
// Generic row-major layout — engine pays per-element swizzle every fetch.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, /*offset*/ 0, /*stride*/ 64,
                    MATRIX_LAYOUT_ROW_MAJOR);    // not optimal
}
```

### Good

```hlsl
// Inference-optimal layout — driver maps to the native tensor-core swizzle.
ByteAddressBuffer g_WeightsOpt : register(t1);   // pre-converted at upload

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_WeightsOpt, /*offset*/ 0, /*stride*/ 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
```

## Options

none

## Fix availability

**suggestion** — The matrix layout conversion happens application-side via `ID3D12LinearAlgebra`. The diagnostic names the call site and the current layout; the application code change is left to the developer.

## See also

- Related rule: [coopvec-fp8-with-non-optimal-layout](coopvec-fp8-with-non-optimal-layout.md) — sibling rule with stricter UB stance for FP8
- Related rule: [coopvec-stride-mismatch](coopvec-stride-mismatch.md) — companion validation
- Related rule: [coopvec-base-offset-misaligned](coopvec-base-offset-misaligned.md) — companion validation
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: [cooperative-vector overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-non-optimal-matrix-layout.md)

*© 2026 NelCit, CC-BY-4.0.*
