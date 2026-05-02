---
id: isnormal-pre-sm69
category: math
severity: error
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# isnormal-pre-sm69

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A call to the `isnormal` intrinsic in a shader compiled against a target shader model older than SM 6.9. The `isnormal` intrinsic — which returns true when the argument is a normal IEEE float (not zero, not subnormal, not infinity, not NaN) — was added in SM 6.9. Earlier targets do not implement it and DXC issues a hard compile error. Slang reflection provides the target SM; the rule reads the target and fires when `isnormal` is called against any pre-SM-6.9 target.

## Why it matters on a GPU

`isnormal` is a portable numerical-classification primitive that mirrors the C99 `isnormal` semantic. Before SM 6.9, authors hand-rolled the equivalent check using bit-cast tricks: `asuint(x) & 0x7F800000` against the IEEE exponent field, with separate tests for zero and subnormal. The hand-rolled form costs 4-6 instructions, plus a wave-wide bit-mask construction; the SM 6.9 intrinsic compiles to a single per-lane test on every IHV (NVIDIA Ada Lovelace, AMD RDNA 3/4, Intel Xe-HPG) because the floating-point classification logic is exposed by the underlying ISA.

Calling `isnormal` against a pre-6.9 target is not a perf regression — it's a hard build break. DXC produces an error pointing at the intrinsic; the lint catches it earlier and points the author at the target SM. The fix is one of: bump the project's target SM to 6.9, replace the call with the bit-cast equivalent, or guard the call behind a compile-time SM check.

The diagnostic emits a candidate bit-cast rewrite as a comment so the author can choose without leaving the file.

## Examples

### Bad

```hlsl
// Compiled with -T cs_6_8 — DXC: error: 'isnormal' undeclared identifier
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    float x = LoadFp(tid);
    if (isnormal(x)) { /* ... */ }   // ERROR on SM < 6.9
}
```

### Good

```hlsl
// Bit-cast equivalent (works on every SM).
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    float x = LoadFp(tid);
    uint  u = asuint(x);
    uint  e = (u >> 23) & 0xFFu;
    bool  isNormal = (e != 0u) && (e != 0xFFu);   // not subnormal, not inf/NaN
    if (isNormal) { /* ... */ }
}
```

## Options

none

## Fix availability

**suggestion** — The bit-cast rewrite is a structured replacement that the linter can emit as a comment, but choosing it vs. bumping the target SM is an authorial decision.

## See also

- Related rule: [isspecialfloat-implicit-fp16-promotion](isspecialfloat-implicit-fp16-promotion.md) — sibling SM 6.9 intrinsic rule
- Related rule: [compare-equal-float](compare-equal-float.md) — companion floating-point hygiene rule
- HLSL specification: [SM 6.9 numerical-classification intrinsics](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_9.html)
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/isnormal-pre-sm69.md)

*© 2026 NelCit, CC-BY-4.0.*
