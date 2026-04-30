---
id: manual-distance
category: math
severity: warn
applicability: machine-applicable
since-version: "v0.2.0"
phase: 2
---

# manual-distance

> **Status:** pre-v0 — rule scheduled for Phase 2; see [ROADMAP](../../ROADMAP.md).

## What it detects

The expression `length(a - b)` where `a` and `b` are vector expressions. The rule matches a call to the `length` intrinsic whose sole argument is a subtraction of two vector operands. It applies to all vector types (`float2`, `float3`, `float4`, and their typed equivalents). It does not fire when the argument to `length` is not a binary subtraction, or when the subtraction is part of a larger expression before being passed to `length`.

## Why it matters on a GPU

`length(v)` computes `sqrt(dot(v, v))`. When given `a - b` as its argument, the sub-expression `a - b` is computed first (one subtraction per component), then passed to `sqrt(dot(..., ...))`. This involves: a vector subtract (3 ops for `float3`), a dot product (3 multiplies + 2 adds = 5 ops), and a `sqrt` at quarter-rate TALU throughput. The total for `float3` is roughly 8 full-rate ops plus one quarter-rate `sqrt`.

`distance(a, b)` is the HLSL intrinsic that computes exactly `length(a - b)`, and the compiler lowers it knowing the full context. In practice, `distance` and `length(a - b)` produce the same instruction count on most current GPU compilers — but using `distance` enables two distinct benefits. First, on some architectures and compiler versions the optimizer can fold the subtraction into the dot product argument more aggressively when it sees the canonical `distance(a, b)` pattern. Second, and more importantly, if both `a` and `b` are expressions with side effects or loads from buffers, the compiler can prove there is no aliasing between the two distinct named arguments to `distance`, enabling better scheduling. With `length(a - b)`, the dependency on the `a - b` temporary must be resolved before any load scheduling decision is made.

The pattern also frequently appears in shaders that compute point-in-sphere tests, light-distance falloff, or particle-to-particle interaction ranges. In those contexts the idiomatic form is `distance(a, b) < r`, which itself triggers [length-comparison](length-comparison.md) for a further optimization. Writing `distance` at this stage makes the subsequent optimization opportunity visible to both the tool and the reader.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 68 — HIT(manual-distance)
float manual_distance(float3 a, float3 b) {
    return length(a - b);   // sub + dot + sqrt; distance() is the canonical form
}
```

### Good

```hlsl
// After machine-applicable fix:
float manual_distance(float3 a, float3 b) {
    return distance(a, b);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `length(a - b)` with `distance(a, b)` is a pure textual substitution. The HLSL specification defines `distance(a, b)` as `length(a - b)`, so the results are identical for all finite inputs. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [length-comparison](length-comparison.md) — further optimization: `distance(a, b) < r` → `dot(a-b, a-b) < r*r`
- Related rule: [inv-sqrt-to-rsqrt](inv-sqrt-to-rsqrt.md) — when the reciprocal distance is needed, prefer `rsqrt(dot(d, d))` over `1.0 / distance(a, b)`
- HLSL intrinsic reference: `distance`, `length` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-distance.md)

*© 2026 NelCit, CC-BY-4.0.*
