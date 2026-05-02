---
id: coopvec-fp8-with-non-optimal-layout
category: cooperative-vector
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# coopvec-fp8-with-non-optimal-layout

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A cooperative-vector matrix multiply whose interpretation enum names an FP8 component type (`COMPONENT_TYPE_FLOAT_E4M3`, `COMPONENT_TYPE_FLOAT_E5M2`) and whose matrix-layout enum is *not* one of the optimal layouts (`MATRIX_LAYOUT_INFERENCING_OPTIMAL` / `MATRIX_LAYOUT_TRAINING_OPTIMAL`). The SM 6.9 cooperative-vector specification mandates an optimal layout for FP8 matrices: the tensor-engine's FP8 path on every IHV requires the vendor-swizzle layout to function correctly. Generic row-major / column-major FP8 is undefined behaviour.

## Why it matters on a GPU

FP8 (E4M3 and E5M2) is the SM 6.9 cooperative-vector path's lowest-precision data type and the one with the highest throughput on the tensor engines. NVIDIA Ada Lovelace's tensor cores execute FP8 matmul at roughly 2x the FP16 throughput; AMD RDNA 3/4's WMMA-FP8 path matches; Intel Xe-HPG's XMX engines have the same approximate ratio. The throughput advantage requires the engine's native FP8 storage layout — the hardware fetcher assumes the bytes are pre-arranged in the swizzle pattern that lets one fetch deliver one tensor-core operand.

Generic row-major or column-major FP8 storage is not just slow on this path; the tensor engine's fetcher does not know how to load it as FP8. The DXC validator recognises the combination as illegal and emits an error; if the validator misses a form (for instance, when the layout enum is computed indirectly), the runtime behaviour is implementation-defined and routinely produces NaN-laced outputs because the byte stream is interpreted at the wrong offsets.

This is why the rule is severity error rather than warn: the no-fix-available result is silent numerical garbage, not a perf regression. Surfacing the violation at lint time replaces a possibly-confusing DXC validation error with a source-located diagnostic that names both the interpretation type and the layout argument.

## Examples

### Bad

```hlsl
// FP8 with row-major — undefined behaviour; tensor engine misreads bytes.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<uint8_t, 16> packedInput = LoadFp8Input(tid);
    vector<float,   16> output;
    MatrixVectorMul(
        output,
        packedInput, COMPONENT_TYPE_FLOAT_E4M3,
        g_Weights, /*offset*/ 0, /*stride*/ 64,
        COMPONENT_TYPE_FLOAT_E4M3,
        MATRIX_LAYOUT_ROW_MAJOR);              // ERROR: FP8 requires OPTIMAL
}
```

### Good

```hlsl
// FP8 with the inference-optimal layout — engine fetches one operand per tx.
ByteAddressBuffer g_WeightsOpt : register(t1);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<uint8_t, 16> packedInput = LoadFp8Input(tid);
    vector<float,   16> output;
    MatrixVectorMul(
        output,
        packedInput, COMPONENT_TYPE_FLOAT_E4M3,
        g_WeightsOpt, /*offset*/ 0, /*stride*/ 64,
        COMPONENT_TYPE_FLOAT_E4M3,
        MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
```

## Options

none

## Fix availability

**none** — The matrix must be uploaded with the optimal layout. The fix is application-side via `ID3D12LinearAlgebra`; the linter cannot perform it.

## See also

- Related rule: [coopvec-non-optimal-matrix-layout](coopvec-non-optimal-matrix-layout.md) — perf-tier sibling rule
- Related rule: [coopvec-stride-mismatch](coopvec-stride-mismatch.md) — companion validation
- Related rule: [coopvec-transpose-without-feature-check](coopvec-transpose-without-feature-check.md) — companion validation
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: [cooperative-vector overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-fp8-with-non-optimal-layout.md)

*© 2026 NelCit, CC-BY-4.0.*
