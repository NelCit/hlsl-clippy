---
id: cross-with-up-vector
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# cross-with-up-vector

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `cross(v, c)` (or the symmetric `cross(c, v)`) where `c` is a `float3` literal whose components are all zero except for one component which is exactly 1.0 or -1.0 — an axis-aligned constant such as `float3(0, 1, 0)` (the conventional up vector), `float3(1, 0, 0)`, `float3(0, 0, 1)`, or any of their negations. The rule matches inline-literal forms and named-constant forms when the constant's value is visible at parse time. The detector is structural on the literal contents: it does not fire when the constant vector has more than one non-zero component (a cross product with a true off-axis constant has no closed-form swizzle simplification), and does not fire when the constant comes from a constant buffer or any other runtime source.

## Why it matters on a GPU

`cross(a, b)` lowers to the standard formula `(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)` — six FP32 multiplies and three FP32 subtracts, or six MAD-shaped instructions when the compiler can fold the negate into the second multiply. On AMD RDNA 3 that is six `v_fma_f32` issues at full VALU rate; on NVIDIA Ada Lovelace, six `FFMA`/`FFMA.NEG` instructions. With one operand fixed to an axis, four of those multiplies are by zero and one is by ±1, so the entire expression collapses algebraically: `cross(v, float3(0, 1, 0))` is exactly `float3(-v.z, 0, v.x)`, `cross(v, float3(1, 0, 0))` is `float3(0, -v.z, v.y)` (note: the y component is `v.z * 0 - v.x * 0 = 0` is not quite right — the actual identity is `float3(0, v.z, -v.y)`), and so on. The rewrite turns six multiplies and three adds into one swizzle plus one negation — a roughly 8x VALU reduction per call site.

Like the dot-product axis case, the compiler will sometimes catch this via constant folding and `0 * x = 0` plus `1 * x = x`. In practice DXC and Slang fold reliably at `-O3` for the inline-literal form on the DXIL backend, but the SPIR-V and Metal backends are less consistent, and even on DXIL the fold is fragile when the literal is wrapped in a `static const float3 UP = ...` declaration or passed through an `inline` helper. The cross-product case is also more sensitive to the literal-folding heuristic than the dot-product case because it produces a vector result: the optimiser must propagate the zero-component knowledge through three separate output components, and partial folding is common (one component eliminated, the other two left as full mul-sub sequences).

The pattern is endemic in camera and orientation code: building a right-vector as `cross(forward, float3(0, 1, 0))`, computing a billboard quad's tangent with `cross(viewDir, worldUp)`, and the standard tangent-frame fallback that picks an arbitrary axis to cross against when the actual tangent is not supplied with the vertex stream. These appear in vertex and geometry shaders that run for every visible vertex, and in skybox and particle shaders that run per-pixel. Eliminating six multiplies per invocation across millions of invocations per frame is large enough to register on a frame-time profile.

## Examples

### Bad

```hlsl
// Six multiplies, three adds — for what reduces to (-v.z, 0, v.x) plus a sign.
float3 right_from_forward(float3 forward) {
    return cross(forward, float3(0, 1, 0));
}

// X-axis cross: the algebraic identity is float3(0, v.z, -v.y).
float3 cross_x(float3 v) {
    return cross(v, float3(1, 0, 0));
}

// Z-axis cross: the algebraic identity is float3(v.y, -v.x, 0).
float3 cross_z(float3 v) {
    return cross(v, float3(0, 0, 1));
}
```

### Good

```hlsl
// One swizzle, one negation — eight cycles down to ~one.
float3 right_from_forward(float3 forward) {
    return float3(forward.z, 0.0, -forward.x);
}

float3 cross_x(float3 v) {
    return float3(0.0, v.z, -v.y);
}

float3 cross_z(float3 v) {
    return float3(v.y, -v.x, 0.0);
}
```

## Options

none

## Fix availability

**machine-applicable** — The closed-form simplification of `cross(v, e_i)` for any cardinal-axis constant `e_i` is exact: the eliminated multiplies are by zero (mathematically exact) and the surviving multiplies are by ±1 (also exact). The fix tool selects the correct swizzle and signs based on the literal vector and the argument order (cross is anti-commutative; `cross(c, v) = -cross(v, c)` and the fix flips signs accordingly). `hlsl-clippy fix` applies the rewrite automatically.

## See also

- Related rule: [dot-on-axis-aligned-vector](dot-on-axis-aligned-vector.md) — companion rule for `dot` with axis-aligned constants
- Related rule: [mul-identity](mul-identity.md) — broader family of multiplication-by-constant simplifications
- HLSL intrinsic reference: `cross`, vector swizzle syntax in the DirectX HLSL language reference
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/cross-with-up-vector.md)

*© 2026 NelCit, CC-BY-4.0.*
