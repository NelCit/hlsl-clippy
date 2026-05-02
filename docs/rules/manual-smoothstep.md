---
id: manual-smoothstep
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# manual-smoothstep

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A hand-rolled cubic Hermite interpolation that implements the body of the `smoothstep` intrinsic. Specifically, the rule matches the sequence:

```
float t = saturate((x - edge0) / (edge1 - edge0));
return t * t * (3.0 - 2.0 * t);
```

or structurally equivalent forms where the clamped normalised parameter `t` (or `n`) is squared twice and combined with the `3 - 2t` polynomial. Minor variations — using intermediate variables, different whitespace, or reordering of the constant factors — are accepted. The rule does not fire on partial implementations (e.g., only the polynomial part without the `saturate`), or on implementations that use a different polynomial (e.g., `t*t*t*(t*(6t-15)+10)`, the higher-order `smootherstep`).

## Why it matters on a GPU

The cubic Hermite polynomial `t*t*(3 - 2*t)` is 5 FP32 operations for a scalar input: one multiply (for `t*t`), one multiply-by-2, one subtract, one multiply (for `t*t * (3 - 2*t)`). The preceding `saturate((x - edge0) / (edge1 - edge0))` adds a subtract, a subtract, a division (or reciprocal-multiply), and a saturate — roughly 4 more operations, with the division typically being a 2-cycle `v_rcp_f32` sequence. Total: approximately 10 scalar FP32 instructions.

`smoothstep(edge0, edge1, x)` is an HLSL intrinsic that the driver and compiler can lower with knowledge of the full computation. On AMD RDNA hardware, the compiler can fold the normalisation and the polynomial into a single expression and apply constant hoisting (if `edge0` and `edge1` are uniform, the reciprocal `1.0 / (edge1 - edge0)` is lifted out of the wave automatically). More importantly, the compiler can recognise `smoothstep` as a single operation and apply MAD folding across the entire sequence, reducing the dependent-multiply chain with optimal FMA scheduling. The instruction count is not necessarily lower than the manual form — but the compiler-visible semantic unit enables better latency hiding across surrounding instructions.

Readability and maintenance are also significant: the manual form requires a reviewer to recognise the Hermite polynomial to verify correctness; `smoothstep` is immediately clear. Bugs in the manual form (wrong constant: `2.0` vs `3.0`, missing `saturate`) are silent shader errors that produce incorrect rendering without a compile-time diagnostic. The intrinsic is guaranteed correct by the shader compiler and driver.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, lines 78-81 — HIT(manual-smoothstep)
float manual_smoothstep(float a, float b, float t) {
    float n = saturate((t - a) / (b - a));
    return n * n * (3.0 - 2.0 * n);   // cubic Hermite; smoothstep() is identical
}
```

### Good

```hlsl
// After machine-applicable fix:
float manual_smoothstep(float a, float b, float t) {
    return smoothstep(a, b, t);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing the matched hand-rolled form with `smoothstep(edge0, edge1, x)` is a pure textual substitution. The HLSL specification defines `smoothstep(edge0, edge1, x)` as exactly the matched pattern: clamp-normalize then apply the cubic Hermite polynomial. The output is identical for all finite inputs where `edge0 != edge1`. When `edge0 == edge1` the result is implementation-defined (division by zero in both the manual form and the intrinsic); the tool emits a note if the edges are statically equal. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [manual-step](manual-step.md) — the degenerate case: binary 0/1 threshold → `step`
- HLSL intrinsic reference: `smoothstep` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-smoothstep.md)

*© 2026 NelCit, CC-BY-4.0.*
