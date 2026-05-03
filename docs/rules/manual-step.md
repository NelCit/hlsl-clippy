---
id: manual-step
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# manual-step

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A ternary conditional expression of the form `x > a ? 1.0 : 0.0` (or the equivalent `x >= a ? 1.0 : 0.0`, `a < x ? 1.0 : 0.0`, `a <= x ? 1.0 : 0.0`) where the true-branch is the literal 1 and the false-branch is the literal 0. The rule also matches integer forms `x > a ? 1 : 0`. It does not fire when the two branches are not the literal constants 0 and 1, or when the comparison direction does not match the `step` semantics (`step(a, x)` returns 1 when `x >= a`). In HLSL, `step(a, x)` returns `0.0` if `x < a` and `1.0` if `x >= a`.

## Why it matters on a GPU

In GLSL/HLSL shader code, conditional expressions `x > a ? 1.0 : 0.0` are common in material shaders for binary masks, alpha cutouts, and clipping thresholds. On a GPU, a ternary conditional in a shader may compile to a comparison instruction plus a `select`/`conditional move` (e.g., `v_cndmask_b32` on RDNA, `SEL` on Xe-HPG). This is already efficient — there is no branch divergence because the compiler recognises the constant-branch ternary — but the pattern still requires a comparison instruction that produces a boolean predicate and a separate select instruction that consumes it.

The `step(a, x)` intrinsic on many GPU architectures lowers to a single `v_cmp_ge_f32` + implicit mask, or on some hardware to a direct `SETGE` instruction that writes 0.0/1.0 directly to a VGPR without a separate select step. The HLSL compiler can match `step` as a single semantic unit and apply target-specific lowering that is unavailable when looking at a generic ternary expression. Even when the final instruction count is the same, using `step` removes a potential wave-divergence signal: a generic ternary with non-constant branches forces the compiler to analyse whether the condition is wave-uniform; `step` communicates that the output is a pure data value (0 or 1), enabling better wave-scheduling decisions.

The pattern is particularly common in alpha-test shaders (`albedo.a > threshold ? 1.0 : 0.0` as a cutout mask), signed-distance field rendering, and procedural texture masks. In these contexts the `step` form is idiomatic HLSL and is what GPU profiling tools (RGA, Nsight Graphics) expect to see when analysing the compiled ISA.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 73 — HIT(manual-step)
float manual_step(float x, float threshold) {
    return x > threshold ? 1.0 : 0.0;   // comparison + select; step() is the idiom
}
```

### Good

```hlsl
// After machine-applicable fix:
float manual_step(float x, float threshold) {
    return step(threshold, x);   // note argument order: step(edge, x)
}
```

## Options

none

## Fix availability

**machine-applicable** — The substitution is exact: `x > a ? 1.0 : 0.0` becomes `step(a, x)`. Note that `step(a, x)` uses `>=` semantics (`1.0` when `x >= a`), matching the common shader convention for strict `>` threshold tests when the equal case is not meaningful. If the calling code depends on the strict-greater-than distinction at the boundary point, a manual review is warranted; the tool emits a note to this effect when applying the fix. `shader-clippy fix` applies it automatically but flags the boundary note in the diff.

## See also

- Related rule: [manual-smoothstep](manual-smoothstep.md) — the continuous generalisation: hand-rolled cubic Hermite → `smoothstep`
- HLSL intrinsic reference: `step` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/manual-step.md)

*© 2026 NelCit, CC-BY-4.0.*
