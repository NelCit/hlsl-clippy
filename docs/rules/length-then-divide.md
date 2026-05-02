---
id: length-then-divide
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# length-then-divide

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Expressions that normalise a vector by manually dividing it by its own length, in any of these forms: the inline `v / length(v)`, the temporary form `float l = length(v); ... v / l;`, the multiply-by-reciprocal form `v * (1.0 / length(v))`, and the rsqrt-of-dot form `v * rsqrt(dot(v, v))` where the `dot` argument matches the multiplied vector. The rule keys on a vector expression that is either divided by `length()` of the same vector, or multiplied by the reciprocal or `rsqrt`-of-self-dot of the same vector. It does not fire when the divisor is a length of a different vector (`v / length(w)` is a legitimate scaling operation), nor when the vector is divided by a stored magnitude that may have been computed earlier and is reused.

## Why it matters on a GPU

`normalize(v)` is an HLSL intrinsic that lowers to the canonical `v * rsqrt(dot(v, v))` sequence on every shipping GPU compiler. The manual `v / length(v)` lowers to `v / sqrt(dot(v, v))`, which is materially worse on the hardware. On AMD RDNA 3, `v_sqrt_f32` and `v_rsq_f32` both occupy the transcendental unit at one-quarter VALU throughput, so the square-root step is the same cost. The difference is the divide: GPU FP division is not a single-cycle operation. RDNA 3 implements scalar FP32 divide as a software macro built on `v_rcp_f32` (one transcendental issue) plus a Newton-Raphson refinement step, totalling roughly 3-5 effective cycles. NVIDIA Ada Lovelace lowers FP32 divide similarly, with `MUFU.RCP` plus refinement on the multi-function unit. The `rsqrt`-and-multiply path replaces the divide with one rcp-equivalent transcendental and one full-rate multiply per vector component — a net saving of 2-4 cycles per normalisation site over the divide form, before counting the vector multiply that the divide must do per-component anyway.

The `v * (1.0 / length(v))` rewrite that some authors apply as a hand optimisation is closer to optimal but still strictly worse than `normalize`: it materialises `1.0 / length(v)` as `rcp(sqrt(...))`, two transcendental issues, where `rsqrt` is one. On RDNA 3 the `v_rsq_f32` instruction directly produces the reciprocal square root in a single SFU slot; computing `rcp(sqrt(x))` separately doubles the transcendental traffic on a unit that is already shared with `pow`, `sin`, `cos`, `log2`, and `exp2`. Across a vertex shader that normalises tangent, bitangent, and normal vectors per vertex, halving the SFU traffic is directly visible in geometry-bound frame timings.

The pattern is most common in code translated from CPU C++ math libraries where divide is essentially free, and in shader code that was originally written for older GPUs where the optimiser was assumed to recognise the idiom. Modern compilers usually do recognise `v / length(v)`, but the recognition is not contractual — `normalize(v)` makes the intent explicit and guarantees the canonical lowering on every backend, including SPIR-V and Metal where the divide-recognition heuristic is less mature.

## Examples

### Bad

```hlsl
// FP divide (rcp + Newton-Raphson) plus sqrt — quarter-rate transcendental and
// extra refinement cycles vs. a single rsqrt.
float3 manual_normalize(float3 v) {
    return v / length(v);
}

// Two transcendental issues (rcp + sqrt) where one (rsqrt) would do.
float3 manual_normalize_rcp(float3 v) {
    return v * (1.0 / length(v));
}
```

### Good

```hlsl
// Lowers to v * rsqrt(dot(v, v)) — one transcendental, one vector multiply.
float3 unit_v(float3 v) {
    return normalize(v);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `v / length(v)` (and the equivalent `v * (1.0 / length(v))` and `v * rsqrt(dot(v, v))` forms) with `normalize(v)` is a pure semantic-preserving substitution. For a non-zero finite `v`, both forms produce a unit vector to within one-ulp transcendental precision; `normalize` is the canonical form and is at least as accurate. `hlsl-clippy fix` applies it automatically. Note that neither form is well-defined for the zero vector — both produce NaN/Inf — so the fix preserves the existing edge-case behaviour.

## See also

- Related rule: [redundant-normalize](redundant-normalize.md) — once `normalize` is in use, detects the further mistake of nesting it
- Related rule: [inv-sqrt-to-rsqrt](inv-sqrt-to-rsqrt.md) — same transcendental-unit argument for `1.0 / sqrt(x)` outside the normalisation context
- Related rule: [manual-distance](manual-distance.md) — companion rule for `length(a - b)` patterns
- HLSL intrinsic reference: `normalize`, `length`, `rsqrt` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/length-then-divide.md)

*© 2026 NelCit, CC-BY-4.0.*
