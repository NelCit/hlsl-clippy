---
id: inv-sqrt-to-rsqrt
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# inv-sqrt-to-rsqrt

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

The expression `1.0 / sqrt(x)` — or `1.0f / sqrt(x)`, `rcp(sqrt(x))`, or a division by `sqrt` of any sub-expression — where the numerator is the literal constant 1. The rule matches the structural pattern of a reciprocal of a square root, regardless of whether `x` is a scalar, vector component, or a compound expression such as `dot(v, v)`. It does not fire when the numerator is not a constant 1, or when the division is by something other than `sqrt(...)`.

## Why it matters on a GPU

On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sqrt` is a transcendental instruction (`v_sqrt_f32` on RDNA, `SQRT` on Xe-HPG) executing at one-quarter peak VALU throughput. A reciprocal (`rcp`, `v_rcp_f32`) is also a special-function-unit instruction, again at quarter rate. The expression `1.0 / sqrt(x)` therefore chains two quarter-rate instructions — sqrt, then rcp — for an effective cost of roughly 8 full-rate cycles per wave on most architectures.

`rsqrt(x)` is a single special-function-unit instruction (`v_rsq_f32` on RDNA, `RSQ` on Xe-HPG) that computes the reciprocal square root in one operation. It executes at the same quarter-rate throughput as a single `sqrt` or `rcp` in isolation, cutting the total transcendental cost in half. This is a well-known GPU optimization: the inverse square root is a primitive operation precisely because it appears in the hot path of nearly every vector normalization and lighting calculation. Dropping from two quarter-rate operations to one frees a transcendental slot per call site that could otherwise be occupied by `sin`, `cos`, `exp2`, or other intrinsics in the same basic block.

The fixture at line 31-33 shows `1.0 / sqrt(dot(v, v))` — a manually computed reciprocal vector length used to normalize without calling `normalize()`. This pattern appears frequently in compute shaders doing particle physics, cloth simulation, or any work that needs the reciprocal length directly (multiplying by `rsqrt` rather than dividing by `sqrt` avoids a second instruction). Both paths produce the same mathematical result for `x > 0`; the `rsqrt` path does so with half the transcendental overhead.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 31 — HIT(inv-sqrt-to-rsqrt)
float inverse_length(float3 v) {
    return 1.0 / sqrt(dot(v, v));   // sqrt (quarter-rate) + rcp (quarter-rate)
}

// Also triggers: scalar case
float inv_len(float x) {
    return 1.0 / sqrt(x);
}
```

### Good

```hlsl
// After machine-applicable fix:
float inverse_length(float3 v) {
    return rsqrt(dot(v, v));   // single quarter-rate rsqrt instruction
}

float inv_len(float x) {
    return rsqrt(x);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `1.0 / sqrt(x)` with `rsqrt(x)` is a pure textual substitution. The result is mathematically identical for `x > 0`. For `x == 0` both forms produce infinity or undefined behaviour; the `rsqrt` form matches existing GPU hardware semantics. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [length-comparison](length-comparison.md) — avoids `length()` entirely for comparisons via `dot(v,v)`
- HLSL intrinsic reference: `rsqrt`, `sqrt` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/inv-sqrt-to-rsqrt.md)

*© 2026 NelCit, CC-BY-4.0.*
