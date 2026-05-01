---
id: select-vs-lerp-of-constant
category: math
severity: warn
applicability: suggestion
since-version: v0.2.0
phase: 2
---

# select-vs-lerp-of-constant

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

Calls to `lerp(K1, K2, t)` where both `K1` and `K2` are compile-time constant scalars or constant vector literals, and `t` is a runtime value. The constants may appear as numeric literals (`0.5`, `float3(1, 0, 0)`), as named `static const` declarations whose initialiser is itself constant, or as expressions that fold to constants at parse time (`1.0 / 3.0`, `2.0 * PI`). The rule does not fire when either endpoint is a runtime expression — that case is the operation `lerp` exists to express. It also does not fire when both endpoints *and* `t` are constants, because that is dead code the compiler folds outright (and a separate constant-folding rule is the right home for it).

## Why it matters on a GPU

`lerp(K1, K2, t)` is mathematically `K1 + (K2 - K1) * t`. With both `K1` and `K2` known at compile time, `K2 - K1` is itself a constant — call it `D` — and the entire expression collapses to a single fused multiply-add: `mad(t, D, K1)`. On AMD RDNA 2/3 that is one `v_fma_f32` issued at full VALU rate; on NVIDIA Turing and Ada Lovelace, one `FFMA`; on Intel Xe-HPG, one FMA on the EU. When the compiler sees the constant operands and folds `K2 - K1` ahead of time, the call costs one ALU cycle. When the compiler does *not* fold — and this is where the rule earns its keep — the call costs two: one to compute `K2 - K1` at runtime, one for the FMA, plus an extra register to hold the temporary.

In practice, DXC reliably folds `lerp(literal, literal, t)` for scalar float literals in the inline form on the DXIL backend at `-O3`. The fold becomes fragile in three cases: (1) the constants are wrapped in `static const` declarations and the fold pass loses track of their constness when the declaration is in a different translation unit or behind an `inline` helper; (2) the constants are vector literals (`float3(0.2, 0.7, 0.1)`) where the fold has to propagate through three components and partial folding is common — one component eliminated, the others left as full sub+mad sequences; (3) the SPIR-V and Metal back-ends do not always replicate DXIL's specific folding heuristic, so the same source can compile to one VALU on D3D12 and two on Vulkan or Metal. The point of the rule is that the *portable* spelling — explicit `mad(t, K2 - K1, K1)` with the subtract written in source — gives the user the one-FMA codegen on every backend without depending on optimiser luck.

A second concern is precision. `lerp(K1, K2, t)` evaluated as `K1 + (K2 - K1) * t` and `mad(t, K2 - K1, K1)` are mathematically identical but lower to the same FMA only when the optimiser proves the equivalence. If the compiler instead lowers `lerp` as `(1 - t) * K1 + t * K2`, that is two multiplies and a subtract — three ops instead of one — and the rounding is worse because it accumulates two products before adding. The literature on `lerp` precision is well-known (the `K1 + (K2 - K1) * t` form is "imprecise at t == 1", the `(1 - t) * K1 + t * K2` form is "monotonic but slower"); on a constant-endpoint `lerp` the linter cannot pick the right policy for the user, so it flags the call and asks for an explicit choice.

## Examples

### Bad

```hlsl
// Up to two VALU + a fragile constant-fold dependency across back-ends.
float remap_unit_to_range(float t) {
    return lerp(0.25, 0.75, t);
}

// Vector literals are the common case where partial folding bites:
float3 sky_gradient(float t) {
    return lerp(float3(0.05, 0.10, 0.20), float3(0.80, 0.90, 1.00), t);
}
```

### Good

```hlsl
// One FMA on every back-end; the constant-fold of K2 - K1 happens at parse time.
float remap_unit_to_range(float t) {
    return mad(t, 0.50, 0.25);  // 0.75 - 0.25 == 0.50
}

float3 sky_gradient(float t) {
    return mad(t, float3(0.75, 0.80, 0.80), float3(0.05, 0.10, 0.20));
}
```

## Options

none

## Fix availability

**suggestion** — The candidate fix rewrites `lerp(K1, K2, t)` to `mad(t, K2 - K1, K1)` with `K2 - K1` evaluated at compile time and the difference written as a literal in the rewritten source. The rewrite is shown as a suggestion rather than machine-applied because of the precision policy choice: in some shaders (UI gradients, reconstructed colour blends) the `(1 - t) * K1 + t * K2` form's monotonicity guarantee at the endpoints matters more than the one-FMA cycle saved. The linter prints both candidate rewrites alongside the original and asks the developer to confirm which precision posture they want before applying.

## See also

- Related rule: [lerp-on-bool-cond](lerp-on-bool-cond.md) — companion rule for the lerp-with-Boolean-fraction case
- Related rule: [lerp-extremes](lerp-extremes.md) — flags `lerp(a, b, 0)` and `lerp(a, b, 1)` constant-fold opportunities
- Related rule: [manual-mad-decomposition](manual-mad-decomposition.md) — surfaces hand-written MAD chains that should be a single `mad`
- HLSL intrinsic reference: `lerp`, `mad` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/select-vs-lerp-of-constant.md)

*© 2026 NelCit, CC-BY-4.0.*
