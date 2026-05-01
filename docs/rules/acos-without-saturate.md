---
id: acos-without-saturate
category: math
severity: warn
applicability: machine-applicable
since-version: v0.4.0
phase: 4
---

# acos-without-saturate

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls to `acos(x)` (and `asin(x)`) where the argument is the result of a `dot(a, b)` between two vectors that are not both provably unit-length, or any other expression whose value is mathematically in `[-1, 1]` but, due to floating-point rounding, can land just outside that range. The canonical pattern is `acos(dot(normalize(a), normalize(b)))` where the two `normalize` calls produce vectors whose dot product can round to `1.0 + epsilon` or `-1.0 - epsilon`; `acos` of an out-of-domain argument returns NaN.

## Why it matters on a GPU

The IEEE 754 specification for `acos` returns NaN for any argument outside `[-1, 1]`. Hardware implementations on AMD RDNA 2/3 (`v_acos_f32`), NVIDIA Turing/Ada (`MUFU.RCOS` family), and Intel Xe-HPG (the transcendental pipe) faithfully implement this: feed `1.0 + 1e-7` and you get a NaN, which then propagates through every subsequent math op into the final colour write. A single NaN pixel in a deferred shading buffer corrupts every subsequent neighbourhood operation (TAA, denoisers, blur), often manifesting as a black or fluorescent-pink dot that grows over a few frames. This is one of the most common GPU correctness bugs in production rendering code.

The arithmetic that causes the out-of-domain value is mundane. `dot(a, b)` for two unit vectors is mathematically `cos(theta)`, which is in `[-1, 1]`. But `normalize(v) = v * rsqrt(dot(v,v))` accumulates rounding: the `rsqrt` is correctly-rounded only on hardware with strict IEEE compliance (most GPUs accept up to 2 ULP error on `rsqrt`), so the resulting "unit" vector has length `1.0 +/- a few ULP`. The dot product of two such vectors is `cos(theta) +/- a few ULP`, which when `theta` is very close to 0 (parallel vectors) can be `1.0 + 5e-8` — well within the rounding budget but outside `acos`'s domain. The same hazard applies to anti-parallel vectors landing at `-1.0 - 5e-8`. Lighting code that computes the angle between a surface normal and a light direction hits this whenever the surface faces the light directly.

The fix is mechanical: wrap the argument in `saturate` (clamps to `[0, 1]`) when the algorithm only cares about non-negative dot products, or `clamp(x, -1.0, 1.0)` for the general case. On AMD RDNA, `saturate` is free — it is an output modifier on most VALU instructions and consumes no extra cycle. On NVIDIA, `saturate` likewise compiles to a SAT modifier on the producing instruction. The cost of the fix is zero cycles; the benefit is eliminating an entire class of NaN bugs. The same pattern applies to `asin`, `sqrt` (any negative argument), and `pow(x, y)` with negative `x` and non-integer `y`.

## Examples

### Bad

```hlsl
float angle_between(float3 a, float3 b) {
    // dot of two normalised vectors is mathematically in [-1, 1] but
    // can round just outside the domain. acos returns NaN.
    return acos(dot(normalize(a), normalize(b)));
}
```

### Good

```hlsl
float angle_between_safe(float3 a, float3 b) {
    // clamp brings the argument back into acos's domain. Free on RDNA
    // and NVIDIA — compiles to a SAT-style output modifier.
    float c = clamp(dot(normalize(a), normalize(b)), -1.0, 1.0);
    return acos(c);
}

// If the algorithm only uses the magnitude of the angle (always
// non-negative), saturate of the absolute value is even cheaper:
float angle_magnitude(float3 a, float3 b) {
    return acos(saturate(abs(dot(normalize(a), normalize(b)))));
}
```

## Options

none

## Fix availability

**machine-applicable** — Wrapping the argument in `clamp(x, -1.0, 1.0)` is a pure semantic preservation when the input was mathematically in domain (the clamp is a no-op for in-domain values). For values that have rounded outside the domain, the clamp produces the mathematically correct boundary value rather than a NaN. `hlsl-clippy fix` applies the wrap automatically; the user can override the rewrite to `saturate(...)` if they know the argument is non-negative.

## See also

- Related rule: [div-without-epsilon](div-without-epsilon.md) — division by potentially-zero divisors
- Related rule: [sqrt-of-potentially-negative](sqrt-of-potentially-negative.md) — `sqrt` of signed expressions
- Related rule: [redundant-saturate](redundant-saturate.md) — when a `saturate` is already in place
- HLSL intrinsic reference: `acos`, `asin`, `clamp`, `saturate` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/acos-without-saturate.md)

*© 2026 NelCit, CC-BY-4.0.*
