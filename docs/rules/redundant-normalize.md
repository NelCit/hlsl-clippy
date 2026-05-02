---
id: redundant-normalize
category: saturate-redundancy
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# redundant-normalize

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls of the form `normalize(normalize(x))` where the outer `normalize` is applied to a vector that is already unit-length because it is the direct result of an inner `normalize` call. The rule matches the direct nested form and the split-variable form where the result of a `normalize` is stored in an intermediate variable and then passed to a second `normalize`. It does not fire when anything other than a `normalize` result is passed to the outer call, even if the value happens to have unit length at runtime.

## Why it matters on a GPU

`normalize(v)` expands to `v * rsqrt(dot(v, v))`. For a 3-component vector, the full computation is: one 3-wide dot product (three multiplies and two adds), one `rsqrt`, and three scalar multiplies (or a 3-wide vector multiply). On AMD RDNA 3, `v_rsq_f32` is a transcendental instruction that issues at 1/4 of the standard VALU rate. On NVIDIA Turing and Ada Lovelace, `MUFU.RSQ` similarly occupies the multi-function unit and is not pipelined at full throughput. The entire sequence — dot product, `rsqrt`, scale — costs roughly 8-10 VALU-equivalent instructions on current GPU hardware.

When `normalize(normalize(v))` is written, both the inner and outer `normalize` execute in full. The output of `normalize` is a unit vector by definition, so `dot(v, v)` for a unit `v` is exactly 1.0, `rsqrt(1.0)` is exactly 1.0, and the outer scale multiplies `v` by 1.0. No numerical work is produced, but the full transcendental sequence still executes — the compiler does not prove across the function boundary (or even within a basic block, in most implementations) that the argument already has unit length. The result is that the 8-10 instruction sequence runs twice for the cost of running it once.

In vertex shaders that transform and re-normalise world-space normals, or in ray-tracing shaders that repeatedly normalise direction vectors as they are passed between functions, this pattern doubles the cost of normalisation without changing results. Eliminating the outer call recovers the full second-normalise budget: the transcendental unit is freed for other `rsqrt`, `rcp`, `sin`, and `cos` calls that share the same execution slot.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/redundant.hlsl, lines 24-27
// HIT(redundant-normalize): normalize is idempotent on its result.
float3 nested_normalize(float3 n) {
    return normalize(normalize(n));
}
```

### Good

```hlsl
// After machine-applicable fix — outer normalize dropped:
float3 nested_normalize(float3 n) {
    return normalize(n);
}
```

## Options

none

## Fix availability

**machine-applicable** — The fix is a pure textual substitution with no observable semantic change. A unit vector is its own normalisation: for any finite non-zero vector `v`, `normalize(normalize(v)) == normalize(v)` in exact arithmetic, and the floating-point error of the double-normalise is strictly larger than the single form. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [redundant-saturate](redundant-saturate.md) — detects `saturate(saturate(x))` by the same idempotence argument
- Related rule: [redundant-transpose](redundant-transpose.md) — detects `transpose(transpose(M))` which is the matrix equivalent of this pattern
- HLSL intrinsic reference: `normalize`, `rsqrt`, `dot` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [saturate-redundancy overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-normalize.md)

*© 2026 NelCit, CC-BY-4.0.*
