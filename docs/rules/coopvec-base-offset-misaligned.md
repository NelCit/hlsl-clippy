---
id: coopvec-base-offset-misaligned
category: cooperative-vector
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# coopvec-base-offset-misaligned

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A cooperative-vector matrix-load call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose constant-folded `offset` argument is not aligned to the cooperative-vector spec's mandated alignment for the chosen component type and layout (typically 16 bytes for float / FP16 / BF16 paths, 64 bytes for the OPTIMAL layouts on most IHVs). The rule walks the constant-fold chain on the offset argument and the alignment annotation surfaced via Slang reflection, then fires on misalignment.

## Why it matters on a GPU

Tensor / matrix engines on every IHV require their source operands to be aligned because the engine's load unit is wired for vector-width transactions. NVIDIA Ada Lovelace's tensor cores fetch operands in 128-bit-aligned groups; AMD RDNA 3/4 WMMA loads through a 128-bit-aligned scalar path; Intel Xe-HPG XMX engines align to the SIMD width. A misaligned base offset either splits the fetch into two transactions (cutting throughput in half) or, on stricter implementations, faults the load — the cooperative-vector spec writes the latter as undefined behaviour to give IHVs the freedom to fail loudly.

The DXC validator catches some misaligned-literal cases; many real-world misalignments come from offsets computed at runtime, where DXC has to defer the check. The lint catches the literal-offset cases with constant-folding before the silent-fault or silent-throughput-loss failure mode appears.

The fix is to round the offset up to the matrix engine's alignment requirement at the application level (the asset loader is the right place) or to align the matrix's base address inside the buffer. The diagnostic names the offset and the required alignment so the upload code can be adjusted.

## Examples

### Bad

```hlsl
// Offset 60 is not 64-byte aligned for an OPTIMAL-layout matrix on most IHVs.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, /*offset*/ 60, /*stride*/ 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);   // misaligned
}
```

### Good

```hlsl
// Offset 64 — properly aligned.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, /*offset*/ 64, /*stride*/ 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}
```

## Options

none

## Fix availability

**none** — The aligned offset is determined by the upload code; the linter names the requirement.

## See also

- Related rule: [coopvec-stride-mismatch](coopvec-stride-mismatch.md) — companion validation
- Related rule: [byteaddressbuffer-load-misaligned](byteaddressbuffer-load-misaligned.md) — same alignment principle for ByteAddressBuffer widened loads
- Related rule: [long-vector-bytebuf-load-misaligned](long-vector-bytebuf-load-misaligned.md) — sibling alignment rule for long-vector loads
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: [cooperative-vector overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-base-offset-misaligned.md)

*© 2026 NelCit, CC-BY-4.0.*
