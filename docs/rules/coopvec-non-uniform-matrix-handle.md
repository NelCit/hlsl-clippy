---
id: coopvec-non-uniform-matrix-handle
category: cooperative-vector
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# coopvec-non-uniform-matrix-handle

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A cooperative-vector matrix-multiply call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose matrix-handle, base-offset, stride, or interpretation argument is wave-divergent. The SM 6.9 cooperative-vector spec marks these arguments as preferring uniform values; non-uniform arguments either serialise across the wave (perf) or, on stricter implementations, produce undefined behaviour. The Phase 4 uniformity analysis (shared with `wave-active-all-equal-precheck` and `cbuffer-divergent-index`) tracks divergence on each argument.

## Why it matters on a GPU

The cooperative-vector matrix engine on every supporting IHV (Ada tensor cores, RDNA 3/4 WMMA, Xe-HPG XMX) executes one matmul per wave, drawing operands from one source matrix per call. The matrix handle, offset, stride, and interpretation arguments parameterise that single matmul; the engine fetches operands once for the whole wave and broadcasts them to lanes.

When any of those arguments is divergent across the wave — different lanes pointing at different matrices, different offsets, different layouts — the engine cannot service the call as a single matmul. On NVIDIA Ada Lovelace's tensor cores, the driver serialises by re-issuing the matmul once per unique tuple of arguments, multiplying the cost by the number of distinct argument tuples in the wave. On AMD RDNA 3/4 WMMA, the same serialisation pattern applies; on Intel Xe-HPG XMX, the implementation may also reject the call as undefined. NVIDIA's cooperative-vector blog cites a 4-32x cost cliff when the matrix handle is divergent across a wave.

The fix is to ensure the matrix handle / offset / stride / interpretation arguments are uniform — typically by hoisting them out of any branch and proving they're computed from cbuffer or wave-uniform values. The diagnostic names the offending argument and the divergence source.

## Examples

### Bad

```hlsl
// Matrix offset is per-lane (depends on tid) — wave serialises.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    uint matOffset = tid * 64;             // per-lane — divergent
    MatrixVectorMul(output, input,
                    g_Weights, matOffset, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
```

### Good

```hlsl
// Matrix offset is uniform across the wave.
ByteAddressBuffer g_Weights : register(t0);
cbuffer Cfg : register(b0) { uint g_MatOffset; }

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, g_MatOffset, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
```

## Options

none

## Fix availability

**suggestion** — Hoisting the argument out of a divergent expression is sometimes mechanical, but more often requires restructuring the calling code. The diagnostic names the divergence source and emits a candidate rewrite when the hoist is straightforward.

## See also

- Related rule: [wave-active-all-equal-precheck](wave-active-all-equal-precheck.md) — same uniformity analysis on a different surface
- Related rule: [cbuffer-divergent-index](cbuffer-divergent-index.md) — same analysis on cbuffer indexing
- Related rule: [coopvec-non-optimal-matrix-layout](coopvec-non-optimal-matrix-layout.md) — companion cooperative-vector rule
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-non-uniform-matrix-handle.md)

*© 2026 NelCit, CC-BY-4.0.*
