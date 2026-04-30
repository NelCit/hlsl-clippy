---
id: redundant-transpose
category: saturate-redundancy
severity: warn
applicability: machine-applicable
since-version: v0.2.0
phase: 2
---

# redundant-transpose

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls of the form `transpose(transpose(M))` where a matrix is transposed twice, yielding the original matrix. The rule matches both the direct nested form and the split-variable form where the result of a `transpose` is stored in an intermediate variable and then passed to a second `transpose`. It does not fire unless both calls are verifiably operating on the same matrix type (same row and column counts), and does not fire on non-square matrices where a type mismatch would make the double-transpose structurally visible. It does fire on `float2x2`, `float3x3`, `float4x4`, and their `halfN` equivalents.

## Why it matters on a GPU

`transpose` in HLSL is a pure value operation: it rearranges the rows and columns of a matrix without any mathematical computation beyond register reassignment. In an ideal pipeline — when the compiler can see both `transpose` calls in the same function and the matrix is stored in contiguous VGPRs — the two calls cancel out and no instructions are emitted. However, this cancellation is not guaranteed across function-call boundaries, across inlining decisions, or when the matrix is passed through a constant buffer or read from a structured buffer.

When the compiler cannot prove the cancellation, it must emit the full register-shuffle sequence for each `transpose`. For a `float4x4`, this is 16 scalar register moves rearranging four 4-wide rows into four 4-wide columns (or the reverse). On RDNA 3 and NVIDIA Ada Lovelace, register moves that cross VGPR alignment boundaries are not free — they may require `v_mov_b32` instructions that occupy VALU issue slots, with throughput depending on how the register allocator has laid out the matrix. On AMD CDNA and Intel Xe-HPG compute targets, where matrices are frequently processed in compute shaders for linear algebra workloads, the cost of a spurious 16-move shuffle is directly subtracted from arithmetic throughput.

The pattern appears in code that interfaces with row-major vs. column-major convention mismatches — a common source of bugs in shader codebases ported between APIs. Authors sometimes add `transpose` calls defensively and then add another when the result appears mirrored, rather than tracing the convention mismatch to its root. The lint rule surfaces these sites so that both calls can be removed together, which also eliminates the underlying convention confusion from the code.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/redundant.hlsl, lines 29-32
// HIT(redundant-transpose): transpose of transpose is the original.
float3x3 nested_transpose(float3x3 m) {
    return transpose(transpose(m));
}
```

### Good

```hlsl
// After machine-applicable fix — both transpose calls dropped:
float3x3 nested_transpose(float3x3 m) {
    return m;
}
```

## Options

none

## Fix availability

**machine-applicable** — The fix is a pure textual substitution with no observable semantic change. `transpose` is an involution: `transpose(transpose(M)) == M` exactly for all finite matrix entries. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [redundant-normalize](redundant-normalize.md) — detects double `normalize` by the same involution argument
- Related rule: [redundant-saturate](redundant-saturate.md) — detects `saturate(saturate(x))` by idempotence
- HLSL intrinsic reference: `transpose` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published — will appear alongside the v0.2.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-transpose.md)

*© 2026 NelCit, CC-BY-4.0.*
