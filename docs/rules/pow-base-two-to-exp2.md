---
id: pow-base-two-to-exp2
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# pow-base-two-to-exp2

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `pow(2.0, x)` — or `pow(2, x)` — where the base is a literal integer or floating-point constant equal to exactly 2.0. The rule fires when the first argument of `pow` is a numeric literal that evaluates to 2.0 after type coercion and the exponent `x` is any expression. It does not fire when the base is a variable, a constant-buffer field, or any non-literal expression.

## Why it matters on a GPU

`pow(2.0, x)` compiles to the full transcendental pair `exp2(x * log2(2.0))`, which simplifies algebraically to `exp2(x * 1.0)` — and then to `exp2(x)`. However, no shipping GPU shader compiler at the HLSL source level recognises this simplification and eliminates the `log2` call at compile time; the `log2(2.0)` is computed at runtime (or at most folded to a constant 1.0 in a separate constant-folding pass that may not combine with the surrounding `exp2`). On AMD RDNA 3, NVIDIA Ada Lovelace, and Intel Xe-HPG, the `pow` lowering path always emits both a `v_log_f32` and a `v_exp_f32` (or their DXIL equivalents), both of which run at one-quarter peak VALU throughput. The resulting instruction cost is the same as `pow(17.3, x)`.

`exp2(x)` maps directly to a single transcendental instruction — `v_exp_f32` on RDNA, `EX2` on Xe-HPG — without a preceding `log2`. The replacement trades two quarter-rate instructions for one, cutting the transcendental unit time in half for this sub-expression. This matters in shaders that convert from linear depth to exponential fog distance, compute HDR exposure curves, or evaluate audio/visual logarithmic-scale quantities expressed as powers of two. Exponential fog and bloom attenuation in particular are common inner-loop patterns in deferred and forward renderers.

The fix is also semantically cleaner: `exp2(x)` is defined for all finite `x`, whereas `pow(2.0, x)` technically relies on the `log2(base)` path which is NaN for non-positive bases — a non-issue when the base is a literal 2.0, but the `pow` codepath still routes through the NaN-sensitive lowering. Replacing with `exp2` signals intent unambiguously to both the compiler and the reader.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 12 — HIT(pow-base-two-to-exp2)
float exponential_falloff(float t) {
    return pow(2.0, -t * 8.0);   // two quarter-rate transcendental instructions
}
```

### Good

```hlsl
// After machine-applicable fix:
float exponential_falloff(float t) {
    return exp2(-t * 8.0);   // single quarter-rate exp2 instruction
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `pow(2.0, x)` with `exp2(x)` is a pure textual substitution. The result is mathematically identical for all finite `x` and is semantically safer (removes an unnecessary `log2` step that would be undefined for non-positive bases). `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [pow-to-mul](pow-to-mul.md) — handles `pow(x, 2.0/3.0/4.0)` → mul expansion
- Related rule: [pow-const-squared](pow-const-squared.md) — focused on the exponent-2 case
- Related rule: [pow-integer-decomposition](pow-integer-decomposition.md) — handles `pow(x, 5.0)` and above
- HLSL intrinsic reference: `exp2`, `pow`, `log2` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pow-base-two-to-exp2.md)

*© 2026 NelCit, CC-BY-4.0.*
