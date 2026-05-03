---
id: manual-reflect
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# manual-reflect

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

The expression `v - 2.0 * dot(n, v) * n` (or algebraically equivalent forms such as `v - 2 * dot(n, v) * n` or `v - dot(n, v) * n * 2.0`) where `v` and `n` are vector expressions. This is the standard reflection formula for reflecting incident vector `v` about a surface normal `n`. The rule matches the structural pattern of a subtract-two-dot-product-scale combination, allowing for commutativity of the scalar factors and the dot-product argument order. It does not fire on partial forms (e.g., only part of the formula) or when the factor is not 2.

## Why it matters on a GPU

The manual expression `v - 2.0 * dot(n, v) * n` decomposes into a `dot` (one FP32 multiply-accumulate per component pair, reducing to a scalar), a scalar multiply by 2.0, a vector-scalar multiply (one multiply per component), and a vector subtract (one subtract per component). For `float3` inputs that is approximately 3 + 1 + 3 + 3 = 10 FP32 operations issued as individual VALU instructions. Depending on MAD folding, a compiler may reduce some of those, but across function boundaries or in inlined code the pattern often remains unoptimised.

`reflect(v, n)` is an HLSL intrinsic that maps to `v - 2 * dot(n, v) * n` semantically, but the compiler can recognise it as a single high-level operation and lower it to a specialised instruction sequence tuned for the target architecture. On AMD RDNA and NVIDIA hardware, the driver can schedule the dependent multiply and subtract operations as a fused sequence that hides latency better than the manually-written equivalent. On some mobile and integrated GPU architectures, `reflect` may lower to a dedicated fixed-function ALU path. Even where the instruction count is identical, using `reflect` communicates intent to the compiler and enables future optimisation without source changes.

Beyond performance, the manual formula is a readability and correctness hazard. The factor must be exactly 2; any typo or future refactor could silently change the reflection angle. `reflect(v, n)` is self-documenting and verified by the shader compiler's semantic analysis. In PBR shaders, `reflect` is used in cube-map environment lookups (reflect the view vector about the surface normal to sample an IBL cube map), in standard specular highlight evaluation, and in ray-tracing any-hit shaders. These call sites are hot; cleaner code also avoids the risk of introducing subtle float-ordering bugs in the manual form.

## Examples

### Bad

```hlsl
// tests/fixtures/phase2/math.hlsl, line 63 — HIT(manual-reflect)
float3 manual_reflect(float3 v, float3 n) {
    return v - 2.0 * dot(n, v) * n;   // 10+ FP32 ops; reflect() maps to same formula
}
```

### Good

```hlsl
// After machine-applicable fix:
float3 manual_reflect(float3 v, float3 n) {
    return reflect(v, n);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing the manual formula with `reflect(v, n)` is a pure textual substitution. The HLSL specification defines `reflect(i, n)` as `i - 2 * dot(n, i) * n`, matching the pattern exactly. `shader-clippy fix` applies it automatically.

## See also

- HLSL intrinsic reference: `reflect` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/manual-reflect.md)

*© 2026 NelCit, CC-BY-4.0.*
