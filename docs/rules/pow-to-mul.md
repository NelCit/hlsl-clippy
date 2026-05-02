---
id: pow-to-mul
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# pow-to-mul

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `pow(x, e)` where the exponent `e` is a literal integer or floating-point constant equal to 2.0, 3.0, or 4.0. The rule fires on any expression whose second argument is a numeric literal that evaluates to exactly 2.0, 3.0, or 4.0 after type coercion. It does not fire when the exponent is a variable, a constant-buffer field, or any non-literal expression. The squared case (`pow(x, 2.0)`) is also covered in detail by [pow-const-squared](pow-const-squared.md); this rule extends coverage to the 3.0 and 4.0 cases and serves as the canonical entry point for the broader mul-expansion family.

## Why it matters on a GPU

`pow(x, e)` is not compiled to repeated multiplication by any shipping GPU compiler. On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, the `pow` intrinsic lowers to the transcendental pair `exp2(e * log2(x))`. Each of `v_log_f32` and `v_exp_f32` on RDNA 3 executes at one-quarter the peak VALU throughput — so a single `pow(x, 3.0)` occupies the equivalent of roughly 4 full-rate ALU cycles, the same cost as `pow(x, 37.5)`. The exponent value 3.0 does not make the instruction cheaper.

A three-way multiply `x * x * x` requires two VALU multiply instructions, both at full rate. On RDNA 3 the VALU can issue one FP32 multiply per clock per SIMD32 lane, so two multiplies cost 2 full-rate cycles versus the ~4 effective cycles of the transcendental path — roughly a 2x reduction on this sub-expression. For `pow(x, 4.0)` the optimal form is `float x2 = x * x; return x2 * x2`, which is still two multiplies (not four) and takes advantage of instruction-level parallelism when the compiler can schedule around a dependent load. Both replace a quarter-rate path with a full-rate one.

Beyond throughput, `log2(x)` is undefined for `x <= 0`, so `pow(x, n)` with a positive literal exponent introduces a latent NaN source that the equivalent mul expansion does not. In a PBR pixel shader, if `x` is a clamped dot product, the linter has no way to verify it is always positive without data-flow analysis; replacing `pow` with multiplies removes the hazard at zero cost. The transcendental unit is also a shared resource on most architectures alongside `sin`, `cos`, `rcp`, and `rsqrt`; eliminating `pow` calls reduces contention when those intrinsics appear in the same basic block.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 17 — HIT(pow-to-mul)
float pow_squared(float x) {
    return pow(x, 2.0);   // compiles to exp2(2.0 * log2(x)), quarter-rate
}

// tests/fixtures/phase2/math.hlsl, line 22 — HIT(pow-to-mul)
float pow_cubed(float x) {
    return pow(x, 3.0);   // same transcendental cost despite the small exponent
}

// Hypothetical four-factor case:
float shininess_term(float n_dot_h) {
    return pow(n_dot_h, 4.0);
}
```

### Good

```hlsl
// After machine-applicable fix:
float pow_squared(float x) {
    return x * x;
}

float pow_cubed(float x) {
    return x * x * x;
}

// Four-factor: reuse the square to keep it two multiplies, not four.
float shininess_term(float n_dot_h) {
    float s = n_dot_h * n_dot_h;
    return s * s;
}
```

## Options

none

## Fix availability

**machine-applicable** — For exponents 2.0 and 3.0 the fix is a straightforward inline expansion with no observable semantic change for finite non-negative inputs. For exponent 4.0, the tool introduces a named temporary to avoid four multiplies; because the temporary is limited to expression scope it does not affect surrounding code. `hlsl-clippy fix` applies all three variants without human confirmation.

## See also

- Related rule: [pow-const-squared](pow-const-squared.md) — focused treatment of the exponent-2 case with additional Schlick Fresnel context
- Related rule: [pow-integer-decomposition](pow-integer-decomposition.md) — generalises to exponents 5 and above using pow-by-squaring
- Related rule: [pow-base-two-to-exp2](pow-base-two-to-exp2.md) — handles `pow(2.0, x)` → `exp2(x)`
- HLSL intrinsic reference: `pow`, `exp2`, `log2` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pow-to-mul.md)

*© 2026 NelCit, CC-BY-4.0.*
