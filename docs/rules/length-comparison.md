---
id: length-comparison
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# length-comparison

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A relational comparison of the form `length(v) < r`, `length(v) > r`, `length(v) <= r`, or `length(v) >= r` where `r` is any scalar expression. The rule also matches `distance(a, b) < r` and related forms (which lower to `length(a - b) < r`). It does not fire when `length` appears in an arithmetic expression rather than a direct comparison, or when the comparison involves expressions that are not a single scalar radius (e.g., `length(v) < f(x)` is still matched, but requires the radius expression to be trivially non-negative for the squared form to be safe; the tool emits a note when it cannot verify this).

## Why it matters on a GPU

`length(v)` computes `sqrt(dot(v, v))`. On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sqrt` is a quarter-rate transcendental instruction (`v_sqrt_f32` on RDNA). Comparing `length(v) < r` introduces a `sqrt` whose only purpose is to produce a value that is immediately compared against `r`. Since `sqrt` is monotonically increasing for non-negative inputs, the comparison `length(v) < r` is equivalent to `dot(v, v) < r * r` for any `r >= 0`. The squared form eliminates the `sqrt` entirely, replacing a quarter-rate transcendental with two full-rate FP32 multiply-adds.

For `float3` input, `dot(v, v)` is 3 FP32 multiply-add instructions at full VALU rate; `r * r` is one multiply. The original form requires those same 3 mads plus a quarter-rate `sqrt`. Eliminating the `sqrt` removes the transcendental bottleneck: on RDNA 3 the TALU executes at 1/4 the VALU rate, so `sqrt` costs the equivalent of 4 full-rate cycles. The replacement costs one extra full-rate multiply (`r * r`) to avoid a 4-cycle transcendental operation — a net saving of approximately 3 equivalent cycles per wave per call site.

This optimisation is particularly valuable in particle collision detection (point-in-sphere test per particle per frame), culling shaders (frustum-sphere intersection), and spatial hash lookups where many per-lane length comparisons are performed per dispatch. The fixture at line 85 shows `length(p) < 1.0` (point-in-unit-sphere test) — the `1.0 * 1.0 = 1.0` constant fold is trivial, so the replacement is literally `dot(p, p) < 1.0`, one `dot` instruction versus one `dot` plus one `sqrt`. In a particle system with 100k particles dispatched at 60 Hz, this single `sqrt` elimination across all dispatch threads is the difference between fitting in the wave occupancy budget and not.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 85 — HIT(length-comparison)
bool inside_unit_sphere(float3 p) {
    return length(p) < 1.0;   // sqrt is quarter-rate; unnecessary for a comparison
}

// General form:
bool within_radius(float3 pos, float3 center, float r) {
    return length(pos - center) < r;
}
```

### Good

```hlsl
// After machine-applicable fix:
bool inside_unit_sphere(float3 p) {
    return dot(p, p) < 1.0;   // 1.0 * 1.0 = 1.0 is a compile-time constant fold
}

bool within_radius(float3 pos, float3 center, float r) {
    float3 d = pos - center;
    return dot(d, d) < r * r;
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `length(v) < r` with `dot(v, v) < r * r` is valid for any `r >= 0`. For `r < 0`, the original `length(v) < r` is always false (length is non-negative), whereas `r * r` would yield a positive right-hand side, making the comparison potentially true — a semantic mismatch. The tool checks whether `r` is statically non-negative (a positive literal, `abs(...)`, a `saturate(...)` output, or similar); if it cannot verify this, it emits a `suggestion` instead of applying automatically. For the common case of a literal or `abs`-wrapped radius, `shader-clippy fix` applies the transformation automatically.

## See also

- Related rule: [manual-distance](manual-distance.md) — `length(a - b)` → `distance(a, b)` as a prior cleanup step
- Related rule: [inv-sqrt-to-rsqrt](inv-sqrt-to-rsqrt.md) — when the reciprocal length is needed instead of a comparison
- HLSL intrinsic reference: `length`, `dot`, `distance` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/length-comparison.md)

*© 2026 NelCit, CC-BY-4.0.*
