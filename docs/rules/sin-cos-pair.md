---
id: sin-cos-pair
category: math
severity: warn
applicability: machine-applicable
since-version: "v0.2.0"
phase: 2
---

# sin-cos-pair

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Separate calls to `sin(x)` and `cos(x)` within the same function body where both calls share the same argument expression `x`. The rule matches any two calls — in any order, any number of statements apart — that operate on the same syntactic argument (same identifier, same literal, or structurally identical sub-expression). It does not fire when only one of the two is present, when the arguments differ, or when the results of both calls are already combined via a `sincos` intrinsic call.

## Why it matters on a GPU

On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sin` and `cos` are transcendental instructions that run on the special-function unit (TALU / transcendental ALU) at one-quarter peak VALU throughput. Two separate calls — `sin(x)` and `cos(x)` — occupy two distinct quarter-rate issue slots: on RDNA 3 that is `v_sin_f32` followed by `v_cos_f32`, each at 1/4 rate, for a combined cost of roughly 8 full-rate ALU-equivalent cycles.

The `sincos(x, s, c)` intrinsic instructs the GPU driver and compiler that both results are needed for the same input. On many architectures the driver can lower this to a single TALU operation that produces both `sin` and `cos` simultaneously, or can at minimum pipeline the two transcendental operations from a single angle-reduction step. The angle-reduction (computing `x mod 2π` and mapping to the unit circle) is the most expensive part of the transcendental evaluation; when computed for `sin` alone, the same range-reduced argument is discarded and recomputed from scratch for `cos`. `sincos` shares that range-reduction across both outputs.

In vertex shaders implementing rotation matrices — a rotation by angle `θ` requires both `sin(θ)` and `cos(θ)` in the matrix entries — and in particle systems applying angular velocity, this pattern is common and hot. The fixture at lines 55-59 shows the exact form: separate `sin(angle)` and `cos(angle)` calls used to fill a `float2`. Replacing with `sincos(angle, s, c)` is a single source change that halves the transcendental cost for that invocation on hardware that fuses the pair.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, lines 55-59 — HIT(sin-cos-pair)
float2 sin_cos_of_same(float angle) {
    float s = sin(angle);   // quarter-rate v_sin_f32; range-reduces angle
    float c = cos(angle);   // quarter-rate v_cos_f32; range-reduces angle again
    return float2(s, c);
}
```

### Good

```hlsl
// After machine-applicable fix:
float2 sin_cos_of_same(float angle) {
    float s, c;
    sincos(angle, s, c);   // single range-reduction, potentially one TALU op
    return float2(s, c);
}
```

## Options

none

## Fix availability

**machine-applicable** — Introducing `sincos(x, s, c)` and replacing the original `sin(x)` and `cos(x)` calls with the output variables is a pure textual transformation with no observable semantic change. The output variables are introduced at declaration scope immediately preceding the `sincos` call. `hlsl-clippy fix` applies it automatically.

## See also

- HLSL intrinsic reference: `sincos`, `sin`, `cos` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/sin-cos-pair.md)

*© 2026 NelCit, CC-BY-4.0.*
