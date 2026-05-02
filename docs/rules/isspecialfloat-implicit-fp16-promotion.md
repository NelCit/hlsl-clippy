---
id: isspecialfloat-implicit-fp16-promotion
category: math
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# isspecialfloat-implicit-fp16-promotion

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A call to one of the SM 6.9 numerical-classification intrinsics (`isnan`, `isinf`, `isfinite`, `isnormal`) with an `fp16` (`half` / `min16float` / `float16_t`) argument compiled against a target where the intrinsic is not implemented natively for fp16, causing the compiler to silently promote the argument to `float` before testing. Slang reflection provides the target SM and the argument type; the rule fires when an fp16 argument widens implicitly.

## Why it matters on a GPU

SM 6.9 added the explicit numerical-classification intrinsics to give authors a portable way to test for IEEE special values. The intrinsics are defined for `float` and `double`; the fp16 path was left to the compiler's promotion rules in the early SM 6.9 drafts and is being tightened in the spec revisions. On NVIDIA Ada Lovelace, the fp16 promotion path costs an extra `cvt.f32.f16` instruction per test on a wave that would otherwise be packed-fp16; on AMD RDNA 3, the WMMA-fp16 lanes are unpacked to scalar fp32 lanes for the test, defeating the packed-math savings. Intel Xe-HPG behaves the same way.

The cost is not catastrophic — one promotion per test — but the surrounding code has usually paid for fp16 storage (groupshared `min16float`, packed cbuffer fields) and expects the test to stay packed. The rule surfaces the silent promotion so the author can either move the value to `float` for the duration of the test or wait for the SM 6.9 spec revision that makes the fp16 form first-class. The diagnostic names the call site and the implicit promotion.

This rule's GPU rationale is partly speculative: the precise cost depends on the driver's instruction-selection heuristic, and the SM 6.9 spec is still tightening the fp16 form. The diagnostic uses "may" language ("may unpack the wave to scalar fp32 for the test") and points the author at the spec.

## Examples

### Bad

```hlsl
// fp16 argument silently promoted to fp32 for the test.
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    half x = LoadFp16(tid);
    if (isnan(x)) { /* x widened to float here */ }
}
```

### Good

```hlsl
// Convert explicitly so the cost is visible.
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    half  x  = LoadFp16(tid);
    float xf = (float)x;
    if (isnan(xf)) { /* test in fp32; explicit cost */ }
}
```

## Options

none

## Fix availability

**suggestion** — The fix is a single explicit cast but the right place to put it depends on whether the surrounding code wants to stay packed-fp16 or not. The diagnostic emits a candidate rewrite as a comment.

## See also

- Related rule: [isnormal-pre-sm69](isnormal-pre-sm69.md) — sibling SM 6.9 intrinsic
- Related rule: [min16float-opportunity](min16float-opportunity.md) — fp16 packing perf rule
- HLSL specification: [SM 6.9 numerical-classification intrinsics](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_9.html)
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/isspecialfloat-implicit-fp16-promotion.md)

*© 2026 NelCit, CC-BY-4.0.*
