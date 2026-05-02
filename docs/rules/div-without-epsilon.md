---
id: div-without-epsilon
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# div-without-epsilon

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Division expressions `a / b` where the divisor `b` is one of: a `length(...)` of an arbitrary vector, a `dot(v, v)` (squared length), a `dot(a, b)` between two vectors that may be perpendicular, the difference of two values that may be equal (`x - y` used as a divisor), or any expression that the rule can statically prove may evaluate to zero on plausible inputs. The same pattern applies to `rcp(b)` and to the implicit reciprocal in `pow(x, -n)` style code.

## Why it matters on a GPU

IEEE 754 single-precision division by zero produces `+inf` (positive numerator) or `-inf` (negative numerator), and `0.0 / 0.0` produces NaN. Both classes of result corrupt the shader's downstream math: `inf - inf` is NaN, `inf * 0` is NaN, any comparison with NaN returns false (so an `if (x > 0)` guard does not catch a NaN from a previous step). The hardware does not flag the operation; on AMD RDNA 2/3, `v_div_f32` is implemented as `v_rcp_f32` followed by `v_mul_f32`, both of which produce IEEE-conforming inf/NaN on degenerate inputs. NVIDIA Turing / Ada use the multi-function unit's `MUFU.RCP` with the same behaviour. Intel Xe-HPG's transcendental pipe is identical.

The bug class arises naturally in real shading code. Normalising a difference vector `(p2 - p1) / length(p2 - p1)` produces NaN when `p1 == p2`. Computing the projection of `a` onto `b` as `dot(a, b) / dot(b, b)` produces NaN when `b` is the zero vector. Reflecting a ray off a surface as `r = d - 2 * dot(d, n) * n` followed by `r / length(r)` blows up when the surface normal is degenerate. Tonemapping operators like `x / (1 + x)` for the Reinhard form are safe (denominator is at least 1), but the local-luminance-divide form `x / Lwhite` where `Lwhite` came from a previous `max` reduction can still hit zero on a fully-black frame. Each of these has shipped in production engines and required hotfix patches.

The fix is to add a small epsilon to the denominator: `a / max(b, 1e-6)` or `a / (b + 1e-6)`. The choice between `max` and additive epsilon depends on whether the divisor can be negative — `max` truncates negatives to the epsilon; additive shifts everything by epsilon. On RDNA and NVIDIA, both forms compile to one extra instruction (a `v_max_f32` or `v_add_f32`), which is essentially free in a typical dependency chain. The semantic change is bounded: for divisors much larger than epsilon, the result is unchanged; for divisors near zero, the result is clamped to a finite (large but representable) value rather than producing inf/NaN. The clamped value is rarely the desired numerical answer, but it is almost always preferable to an inf/NaN that propagates through the rest of the shader.

## Examples

### Bad

```hlsl
float3 normalize_diff(float3 p1, float3 p2) {
    float3 d = p2 - p1;
    // length(d) == 0 when p1 == p2; the divide produces inf/NaN.
    return d / length(d);
}

float3 project(float3 a, float3 b) {
    // dot(b, b) == 0 when b is the zero vector. Same hazard.
    return (dot(a, b) / dot(b, b)) * b;
}
```

### Good

```hlsl
float3 normalize_diff_safe(float3 p1, float3 p2) {
    float3 d = p2 - p1;
    // Or use the built-in `normalize`, which has the same hazard but
    // wrap it: `d * rsqrt(max(dot(d, d), 1e-12))`.
    float  l = length(d);
    return d / max(l, 1e-6);
}

float3 project_safe(float3 a, float3 b) {
    float dd = dot(b, b);
    return (dot(a, b) / max(dd, 1e-12)) * b;
}
```

## Options

- `epsilon` (number, default: 1e-6) — the recommended denominator floor. Shaders working in different numeric ranges (HDR luminance, sub-millimetre geometry) may want a different value.

## Fix availability

**machine-applicable** (since v1.2 — ADR 0019) — The fix wraps the divisor in `max(<epsilon>, <divisor>)`, where `<epsilon>` comes from the project-tuned `Config::div_epsilon()` (see `[float] div-epsilon` in `.hlsl-clippy.toml`, default `1e-6`). The rewrite preserves divisor evaluation count, so the substitution is safe whenever the divisor is **side-effect-free** under the v1.2 purity oracle.

The fix downgrades to **suggestion-only** when the divisor contains an unknown call, an assignment, or any other observable side effect — hand-review before applying.

```toml
# .hlsl-clippy.toml — tune the inserted epsilon to your project's dynamic range.
[float]
div-epsilon = 1e-5
```

The choice between `max(b, eps)`, `b + eps`, and an early-exit `if (b < eps) return ...` remains application-dependent: the linter ships `max(b, eps)` because it preserves register pressure and works on both scalar and vector divisors. If your shader needs a different idiom, mark the rule `allow` for that call site and use the suggestion shape that fits.

## See also

- Related rule: [acos-without-saturate](acos-without-saturate.md) — domain protection for `acos`
- Related rule: [sqrt-of-potentially-negative](sqrt-of-potentially-negative.md) — domain protection for `sqrt`
- Related rule: [redundant-rcp-mul](redundant-rcp-mul.md) — algebraic simplification of `rcp` patterns
- HLSL intrinsic reference: `rcp`, `length`, `dot`, `max` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/div-without-epsilon.md)

*© 2026 NelCit, CC-BY-4.0.*
