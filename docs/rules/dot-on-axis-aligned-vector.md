---
id: dot-on-axis-aligned-vector
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# dot-on-axis-aligned-vector

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `dot(v, c)` (or the symmetric `dot(c, v)`) where `c` is a vector literal whose components are all zero except for one component which is exactly 1.0 or -1.0 — that is, an axis-aligned constant such as `float3(1, 0, 0)`, `float4(0, 0, 1, 0)`, or `float3(0, -1, 0)`. The rule matches both inline-literal forms (`dot(v, float3(1, 0, 0))`) and named-constant forms when the constant's value is visible to the parser as a `static const` initialiser. The match also covers the trivial single-axis selection idioms like `dot(v, float3(1, 0, 0))` for selecting `v.x`. It does not fire when the constant has more than one non-zero component (a true diagonal-axis dot product is a meaningful operation, not a swizzle), and does not fire when the constant is loaded from a constant buffer, structured buffer, or any other runtime source.

## Why it matters on a GPU

`dot` is a high-level intrinsic that lowers to a multiply-and-reduce sequence on every GPU. For a `float3`, `dot(v, c)` lowers to roughly one multiply per component (three FP32 multiplies into a vector temporary) and a two-step horizontal add (two more FP32 adds reducing to a scalar), or, when the compiler can fold the multiplies into MADs, two `v_fma_f32` instructions and one `v_add_f32` on AMD RDNA 3. NVIDIA Ada Lovelace `FFMA`/`FADD` lowering is structurally identical. Even with optimal MAD folding, that is three to five issued VALU instructions for what, when one of the operands is `(1, 0, 0)`, is mathematically just `v.x` — a zero-instruction operation: a swizzle is a free register-name remapping that emits no machine code at all on AMD or NVIDIA hardware (and on Intel Xe-HPG, swizzles are encoded as source-operand modifiers on the consuming instruction, similarly free).

When the linter sees `dot(v, float3(1, 0, 0))` it is staring at five wasted cycles per invocation. The compiler will sometimes catch it via constant folding plus the `0 * x = 0` and `1 * x = x` algebraic identities, but the catch is not contractual — it depends on the front-end aggressively partial-evaluating literal vector arguments, which DXC and Slang both do inconsistently across optimisation levels and target backends (DXIL vs. SPIR-V vs. Metal). At `-O0` or even at default optimisation on the SPIR-V path, the full `dot` sequence is often emitted verbatim. The rewrite to `v.x` (or `-v.y` for `(0, -1, 0)`, etc.) makes the optimisation contractual at the source level and removes the source of the variability.

The pattern shows up in lighting code that extracts a single component of a transformed vector ("brightness along the up axis = `dot(L, float3(0, 1, 0))`"), in tangent-space conversions that pull off a basis-vector projection, and in code that has been generic-parameterised to take an axis vector but is always called with a literal cardinal axis. In a fragment shader that runs at 1920x1080 = 2M lanes per frame, eliminating five cycles per pixel per call site is roughly 10M cycles per frame per site — a measurable share of a 60 Hz budget on integrated GPUs and a non-trivial share on discrete GPUs at 240+ Hz.

## Examples

### Bad

```hlsl
// Three multiplies, two adds — for what is just v.x.
float along_x(float3 v) {
    return dot(v, float3(1, 0, 0));
}

// Negated axis: still a swizzle plus a unary negate, not a dot product.
float along_neg_y(float3 v) {
    return dot(v, float3(0, -1, 0));
}

// Four-component case: same waste, same fix.
float along_w(float4 v) {
    return dot(v, float4(0, 0, 0, 1));
}
```

### Good

```hlsl
// Free swizzle — no instructions emitted on AMD or NVIDIA.
float along_x(float3 v) {
    return v.x;
}

float along_neg_y(float3 v) {
    return -v.y;
}

float along_w(float4 v) {
    return v.w;
}
```

## Options

none

## Fix availability

**machine-applicable** — The rewrite from `dot(v, e_i)` to `v.<i>` (negated when the axis component is -1) is a strictly semantic-preserving simplification: for the axis-aligned constant the algebraic identity is exact, with no rounding-mode considerations because the mul-by-zero terms are exactly zero and the mul-by-one term is exact. `hlsl-clippy fix` applies it automatically and chooses the correct swizzle and sign based on the literal vector contents.

## See also

- Related rule: [cross-with-up-vector](cross-with-up-vector.md) — companion rule for `cross` with axis-aligned constants
- Related rule: [mul-identity](mul-identity.md) — broader family of multiplication-by-constant simplifications
- HLSL intrinsic reference: `dot`, vector swizzle syntax in the DirectX HLSL language reference
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/dot-on-axis-aligned-vector.md)

*© 2026 NelCit, CC-BY-4.0.*
