---
id: pow-integer-decomposition
category: math
severity: warn
applicability: suggestion
since-version: "v0.2.0"
phase: 2
---

# pow-integer-decomposition

> **Status:** pre-v0 — rule scheduled for Phase 2; see [ROADMAP](../../ROADMAP.md).

## What it detects

Calls to `pow(x, e)` where the exponent `e` is a literal integer or floating-point constant equal to an integer value of 5 or greater (up to a configurable ceiling, default 16). The rule fires when the second argument is a numeric literal that evaluates to a whole number in that range. It does not fire when the exponent is a variable, a constant-buffer field, or a non-integer-valued literal. Exponents 2.0, 3.0, and 4.0 are handled by [pow-to-mul](pow-to-mul.md); exponents exceeding the ceiling (default 16) are not flagged because the mul-chain becomes longer than the transcendental cost.

## Why it matters on a GPU

As described in [pow-to-mul](pow-to-mul.md), `pow(x, e)` on AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG always lowers to `exp2(e * log2(x))` regardless of the exponent value. A `pow(x, 5.0)` call carries the same two-quarter-rate-instruction cost as `pow(x, 5000.0)`. For small integer exponents that cost can be replaced by a sequence of full-rate multiply instructions using the pow-by-squaring algorithm: `pow(x, 5)` becomes `x2 = x*x; x4 = x2*x2; return x4*x;` — three multiplies, all at full VALU rate. On RDNA 3, a full-rate FP32 multiply issues at 1 per clock per SIMD32 lane; three of them cost 3 full-rate cycles versus the ~4 effective cycles of the transcendental pair, and critically they occupy the general VALU rather than the shared transcendental unit (TALU).

The Schlick Fresnel approximation `pow(1 - NdotV, 5)` is the canonical appearance of this pattern in PBR shaders, evaluated per-pixel in deferred lighting over the full G-buffer. The fixture file at line 6 (`pow(1.0 - n_dot_v, 5.0)`) and line 28 (`pow(x, 5.0)`) both trigger this rule. Schlick Fresnel at native resolution (4K) with 60 Hz deferred rendering executes this expression across roughly 8.3 million pixels per frame. Replacing the transcendental pair with three multiplies at full rate frees the TALU for co-resident `sin`/`cos`/`rsqrt` calls and reduces total instruction count on the critical path.

For exponents 8 and 16 the squaring chain is maximally efficient: `pow(x, 8)` → `x2=x*x; x4=x2*x2; x8=x4*x4` (3 multiplies); `pow(x, 16)` → 4 multiplies. For non-power-of-two exponents the algorithm adds one multiply per "1" bit in the binary representation. The rule is classified as a `suggestion` rather than `machine-applicable` because the optimal decomposition depends on context: if the same base `x` is used in multiple pow calls in the same scope, a shared intermediate `x*x` may already exist and the tool cannot deduplicate without data-flow analysis.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 4 — HIT(pow-to-mul) and HIT(pow-integer-decomposition)
float3 fresnel_schlick(float n_dot_v, float3 f0) {
    float k = pow(1.0 - n_dot_v, 5.0);   // quarter-rate transcendental pair
    return f0 + (1.0 - f0) * k;
}

// tests/fixtures/phase2/math.hlsl, line 27 — HIT(pow-integer-decomposition)
float pow_decomposable(float x) {
    return pow(x, 5.0);
}

float gloss_highlight(float n_dot_h) {
    return pow(n_dot_h, 8.0);   // 8 = 2^3, optimal: 3 multiplies
}
```

### Good

```hlsl
// After suggested fix — Schlick Fresnel via pow-by-squaring:
float3 fresnel_schlick(float n_dot_v, float3 f0) {
    float v  = 1.0 - n_dot_v;
    float v2 = v * v;
    float k  = v2 * v2 * v;   // (1-NdotV)^5 via 3 multiplies, full-rate VALU
    return f0 + (1.0 - f0) * k;
}

float pow_decomposable(float x) {
    float x2 = x * x;
    float x4 = x2 * x2;
    return x4 * x;   // x^5
}

float gloss_highlight(float n_dot_h) {
    float s2 = n_dot_h * n_dot_h;
    float s4 = s2 * s2;
    return s4 * s4;   // n_dot_h^8 via 3 multiplies
}
```

## Options

- `max-exponent` (integer, default: 16) — The largest integer exponent for which the rule fires. Set lower to restrict to very small exponents; set higher if you want to flag larger decompositions. Exponents above this value are silently ignored.

## Fix availability

**suggestion** — A candidate mul-chain expansion is shown in the diagnostic, but the tool does not apply it automatically. The optimal decomposition depends on whether intermediate powers of `x` are already computed in the surrounding scope; automatic rewriting without data-flow analysis could introduce redundant temporaries that a human reviewer would merge. Review the suggestion and apply manually.

## See also

- Related rule: [pow-to-mul](pow-to-mul.md) — handles the machine-applicable cases for exponents 2.0, 3.0, and 4.0
- Related rule: [pow-const-squared](pow-const-squared.md) — focused treatment of the exponent-2 case
- Related rule: [pow-base-two-to-exp2](pow-base-two-to-exp2.md) — handles `pow(2.0, x)` → `exp2(x)`
- HLSL intrinsic reference: `pow`, `exp2`, `log2` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pow-integer-decomposition.md)

*© 2026 NelCit, CC-BY-4.0.*
