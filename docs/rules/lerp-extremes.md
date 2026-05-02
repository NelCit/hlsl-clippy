---
id: lerp-extremes
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# lerp-extremes

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `lerp(a, b, t)` where the interpolation weight `t` is a numeric literal equal to exactly 0 or 1 (including `0.0`, `1.0`, `0.0f`, `1.0f`). When `t` is 0, `lerp(a, b, 0)` is equivalent to `a`; when `t` is 1, `lerp(a, b, 1)` is equivalent to `b`. The rule fires on scalar, vector, and matrix overloads of `lerp`. It does not fire when `t` is a variable, a constant-buffer field, or a non-literal expression.

## Why it matters on a GPU

The `lerp(a, b, t)` intrinsic compiles to a multiply-add sequence: `a + t * (b - a)`, or on architectures with a native `lerp` instruction, to a single FMA-class operation. On AMD RDNA 3, `v_lerp_u8` exists for packed integer paths; for FP32 the compiler emits `v_fma_f32` (or two instructions: `v_sub_f32` + `v_mad_f32`). Either way, a call with a constant `t` of 0 or 1 performs arithmetic that reduces algebraically to a constant output — yet most GPU compilers do not eliminate the entire `lerp` call at the HLSL source level and instead rely on a later constant-folding pass that may not trigger across function boundaries or after inlining.

When `t = 0.0`, `a + 0.0 * (b - a)` is `a` under IEEE-754, provided `b - a` does not generate a NaN. The subtraction `b - a` and the multiply by zero are nominally dead but still occupy issue slots until the compiler proves them dead. On a wave with 32 or 64 lanes, those are 32 or 64 dead instructions per wave per call site. In a material blending shader that selects between two surface properties based on a mask, the mask endpoint case (fully opaque, fully transparent) is common in practice — one endpoint of the blend is always the trivial case.

When `t = 1.0`, `a + 1.0 * (b - a)` simplifies to `b`; again, without explicit source-level elimination the compiler emits a subtract and a multiply that cancel. Replacing with the direct expression `a` or `b` removes two FP32 instructions from the instruction stream and makes the intent explicit to both the compiler and the reader.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 36 — HIT(lerp-extremes)
float3 lerp_zero_endpoint(float3 a, float3 b) {
    return lerp(a, b, 0.0);   // always returns a; subtract+multiply are dead
}

// tests/fixtures/phase2/math.hlsl, line 41 — HIT(lerp-extremes)
float3 lerp_one_endpoint(float3 a, float3 b) {
    return lerp(a, b, 1.0);   // always returns b; sub+mad are dead
}
```

### Good

```hlsl
// After machine-applicable fix:
float3 lerp_zero_endpoint(float3 a, float3 b) {
    return a;
}

float3 lerp_one_endpoint(float3 a, float3 b) {
    return b;
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `lerp(a, b, 0.0)` with `a` and `lerp(a, b, 1.0)` with `b` are pure textual substitutions with no observable semantic change for finite inputs. `hlsl-clippy fix` applies both variants automatically.

## See also

- Related rule: [mul-identity](mul-identity.md) — analogous dead-arithmetic elimination for `x*1`, `x+0`, `x*0`
- HLSL intrinsic reference: `lerp` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/lerp-extremes.md)

*© 2026 NelCit, CC-BY-4.0.*
