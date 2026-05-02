---
id: mul-identity
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# mul-identity

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Arithmetic expressions where one operand is a numeric literal that makes the operation trivially reducible:

- `x * 1.0` (or `1 * x`) — multiplying by the multiplicative identity; result is `x`.
- `x + 0.0` (or `0 + x`, `x - 0.0`) — adding or subtracting the additive identity; result is `x`.
- `x * 0.0` (or `0 * x`) — multiplying by zero; result is `0.0`.

The rule fires when the literal operand is exactly 0 or 1 (after type coercion) and applies to scalar, vector, and matrix types. It does not fire when either operand is a variable or non-literal expression; such cases require data-flow analysis and may have side effects (NaN propagation through `0 * NaN = NaN` is not equivalent to the literal `0.0`).

## Why it matters on a GPU

Every VALU instruction on a GPU occupies an issue slot. On AMD RDNA 3, the VALU can sustain one FP32 instruction per clock per SIMD32 lane at peak throughput. A multiply-by-one issues an FP32 `v_mul_f32` that reads two source registers, executes a multiply, and writes back — doing mathematically zero work. A GPU compiler may eliminate these in an early simplification pass, but only when the constant is visible and the pass runs before register allocation. Across function boundaries, after constant-buffer loads, or in generated shader code from offline material compilers, this simplification frequently does not happen at the HLSL level.

The `x * 0.0` case merits separate attention. Under IEEE-754, `0.0 * x` is not unconditionally zero: if `x` is NaN or infinity, the result is NaN, not zero. This means a GPU compiler applying `-ffast-math` may legally fold it, but a conformant compiler targeting Direct3D's strict IEEE-754 mode cannot. The result is that `x * 0.0` can appear as a live instruction even in release builds when NaN propagation semantics matter. In contexts where the author genuinely intends a zero (e.g., zeroing out a vector component), replacing with the literal `0.0` or `float3(0, 0, 0)` is both more efficient and clearer in intent.

The fixture at lines 47-52 shows all three patterns in sequence — `v * 1.0`, `a + 0.0`, `b * 0.0` — combined in a single function. In a pixel shader invoked millions of times per frame, each redundant instruction multiplies by the warp count and the dispatch count. Even a single extra `v_mul_f32` per pixel at 4K/60 Hz is approximately 500 million unnecessary instructions per second on that GPU, translating to measurable occupancy and throughput pressure in ALU-bound passes.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, lines 47-52 — HIT(mul-identity) x3
float3 mul_identities(float3 v, float k) {
    float3 a = v * 1.0;    // multiplicative identity: result is v
    float3 b = a + 0.0;    // additive identity: result is a
    float3 c = b * 0.0;    // multiply-by-zero: result is 0 (for finite b)
    return a + b + c + k;
}
```

### Good

```hlsl
// After machine-applicable fix:
float3 mul_identities(float3 v, float k) {
    float3 a = v;
    float3 b = a;
    float3 c = 0.0;   // or float3(0, 0, 0) for the vector case
    return a + b + c + k;
}
```

## Options

none

## Fix availability

**machine-applicable** — All three patterns are pure textual substitutions with no observable semantic change for finite, non-NaN inputs. The `x * 0.0` case assumes the author intends a literal zero rather than NaN propagation; if the surrounding code uses `x * 0.0` deliberately for NaN-sentinel purposes (uncommon), use an inline suppression comment. `hlsl-clippy fix` applies all three variants automatically.

## See also

- Related rule: [lerp-extremes](lerp-extremes.md) — analogous dead-arithmetic elimination for `lerp(a, b, 0)` and `lerp(a, b, 1)`
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mul-identity.md)

*© 2026 NelCit, CC-BY-4.0.*
