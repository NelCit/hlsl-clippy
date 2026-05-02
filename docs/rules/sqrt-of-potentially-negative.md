---
id: sqrt-of-potentially-negative
category: math
severity: warn
applicability: machine-applicable
since-version: v0.4.0
phase: 4
---

# sqrt-of-potentially-negative

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls to `sqrt(x)` (and `rsqrt(x)`) where `x` is a signed expression whose value can be negative on plausible inputs: a subtraction `a - b`, a `1.0 - dot(v, v)` style discriminant where `v` may not be unit-length, a discriminant in a quadratic solver (`b*b - 4*a*c`) without a non-negative guard, or any expression involving a buffer load that the rule cannot prove is non-negative. The rule does not fire on `length(v)`, `dot(v, v)`, or other constructions that are mathematically guaranteed non-negative.

## Why it matters on a GPU

IEEE 754 `sqrt(x)` for `x < 0` returns NaN. The same applies to `rsqrt(x)` for `x < 0` (and `rsqrt(0)` returns `+inf`). On AMD RDNA 2/3, `v_sqrt_f32` and `v_rsq_f32` are transcendental instructions that conform to IEEE for negative inputs by producing NaN; the bit pattern propagates through subsequent VALU operations. NVIDIA Turing, Ada, and Blackwell follow the same convention via `MUFU.SQRT` and `MUFU.RSQ`. Intel Xe-HPG's transcendental pipe is identical. The driver has no way to distinguish "this NaN was a bug" from "this NaN was deliberate", so the result is silently a NaN that contaminates the frame.

The most common source of this hazard is reconstructing a vector's missing component from its squared length. Normal-map decoding for two-channel normals does `nz = sqrt(1.0 - nx*nx - ny*ny)`; if the texture data has any rounding, dequantisation, or filtering artefacts, the inner expression can land at a small negative value. Spherical-harmonics reconstruction does `sqrt(saturate(L0 - L_higher_terms))` for the same reason — the higher-order terms can momentarily exceed the L0 term in over-bright pixels. Quadratic-solver discriminants in ray-sphere intersection (`b*b - 4*a*c`) are negative when the ray misses the sphere; the algorithm may rely on the non-hit case being detected via a different test, but if the `sqrt` runs unconditionally first, the NaN appears.

The fix is to wrap the argument in `max(x, 0.0)` (or `saturate(x)` if the upper bound is also `1.0`). On RDNA and NVIDIA this compiles to a single extra `v_max_f32` instruction or, more often, to a free SAT-style output modifier on the producing instruction. The semantic change is bounded: for non-negative inputs, the wrap is a no-op; for negative inputs, the result is `0` or `+inf` (for `rsqrt`) instead of NaN. Some algorithms genuinely need to detect the negative case (ray-sphere miss); those should branch on `if (discriminant >= 0)` before the `sqrt` rather than relying on the NaN. The `max(x, 0)` wrap is the right default for reconstruction code where a negative input represents accumulated rounding error rather than a meaningful "miss" signal.

## Examples

### Bad

```hlsl
float3 decode_normal(float2 nxy) {
    // 1 - dot(nxy, nxy) can be slightly negative due to texture
    // dequantisation; sqrt of a negative is NaN.
    float nz = sqrt(1.0 - dot(nxy, nxy));
    return float3(nxy, nz);
}

bool ray_hits_sphere(float3 ro, float3 rd, float3 c, float r,
                     out float t) {
    float3 oc = ro - c;
    float  b  = dot(oc, rd);
    float  cc = dot(oc, oc) - r * r;
    float  disc = b * b - cc;
    // sqrt of a negative discriminant when the ray misses the sphere.
    t = -b - sqrt(disc);
    return t > 0.0;
}
```

### Good

```hlsl
float3 decode_normal_safe(float2 nxy) {
    // saturate keeps the argument in [0, 1]; nz is well-defined for any
    // nxy. Costs zero extra cycles on RDNA / NVIDIA (output modifier).
    float nz = sqrt(saturate(1.0 - dot(nxy, nxy)));
    return float3(nxy, nz);
}

bool ray_hits_sphere_safe(float3 ro, float3 rd, float3 c, float r,
                          out float t) {
    float3 oc = ro - c;
    float  b  = dot(oc, rd);
    float  cc = dot(oc, oc) - r * r;
    float  disc = b * b - cc;
    if (disc < 0.0) { t = 0.0; return false; }   // explicit miss test
    t = -b - sqrt(disc);
    return t > 0.0;
}
```

## Options

none

## Fix availability

**machine-applicable** — Wrapping the argument in `max(x, 0.0)` is semantically equivalent for non-negative inputs and replaces NaN with `0.0` for negative inputs (or `+inf` for `rsqrt`). When the algorithm legitimately needs to distinguish negative inputs from non-negative, the user can override the fix to insert an explicit `if`-guard. `hlsl-clippy fix` applies the wrap automatically; for `1.0 - x` style expressions where `x` is a dot product of unit-length-intent vectors, the rewrite uses `saturate(...)` instead of `max(..., 0.0)`.

## See also

- Related rule: [acos-without-saturate](acos-without-saturate.md) — domain protection for `acos`
- Related rule: [div-without-epsilon](div-without-epsilon.md) — division by potentially-zero divisors
- HLSL intrinsic reference: `sqrt`, `rsqrt`, `max`, `saturate` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/sqrt-of-potentially-negative.md)

*© 2026 NelCit, CC-BY-4.0.*
