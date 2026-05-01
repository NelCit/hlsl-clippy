---
id: coopvec-stride-mismatch
category: cooperative-vector
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# coopvec-stride-mismatch

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A cooperative-vector matrix-load call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose constant-folded `stride` argument does not equal the natural row-stride implied by the matrix dimensions and the component type (`rows * sizeof(component)` or `cols * sizeof(component)` depending on layout). The SM 6.9 cooperative-vector specification requires the stride to match the matrix layout exactly when the layout enum is not OPTIMAL; mismatches produce undefined behaviour because the matrix-engine fetcher walks the wrong number of bytes per row.

## Why it matters on a GPU

When a cooperative-vector call uses a generic row-major or column-major layout, the matrix engine on each IHV (Ada tensor cores, RDNA 3/4 WMMA, Xe-HPG XMX) walks the source buffer using the stride argument as the per-row byte advance. The engine assumes the stride is the natural one for the matrix shape and component type; if it isn't, the engine reads garbage bytes from outside the matrix or from the wrong row, and produces NaN-laced or zero results. There is no error signalled at runtime — the tensor engine has no concept of buffer bounds beyond what the stride tells it.

DXC's validator catches the simplest forms (literal-stride mismatch on a literal-shape matrix) but misses forms where the stride or shape is computed. Catching this at lint time uses constant-folding over the AST to recover the literal stride and shape and verifies the relationship, surfacing the mismatch with a precise diagnostic before the developer hits the silent-NaN failure mode.

For OPTIMAL layouts (`MATRIX_LAYOUT_INFERENCING_OPTIMAL` / `MATRIX_LAYOUT_TRAINING_OPTIMAL`), the stride argument is ignored by the runtime; the matrix engine uses the vendor-swizzle layout's intrinsic step. The rule does not fire in that case.

## Examples

### Bad

```hlsl
// 16x16 row-major float matrix should have stride = 64 (16 * sizeof(float)),
// not 32. Tensor engine misreads.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, /*offset*/ 0, /*stride*/ 32,    // WRONG
                    MATRIX_LAYOUT_ROW_MAJOR);
}
```

### Good

```hlsl
// Stride matches the row width.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, /*offset*/ 0, /*stride*/ 64,
                    MATRIX_LAYOUT_ROW_MAJOR);
}
```

## Options

none

## Fix availability

**none** — The right stride depends on the upload-side intent (which dimension is the row); the diagnostic names the mismatch and the expected value.

## See also

- Related rule: [coopvec-base-offset-misaligned](coopvec-base-offset-misaligned.md) — companion validation
- Related rule: [coopvec-non-optimal-matrix-layout](coopvec-non-optimal-matrix-layout.md) — perf rule for non-optimal layouts
- Related rule: [coopvec-fp8-with-non-optimal-layout](coopvec-fp8-with-non-optimal-layout.md) — companion validation
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-stride-mismatch.md)

*© 2026 NelCit, CC-BY-4.0.*
