---
id: comparison-sampler-without-comparison-op
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# comparison-sampler-without-comparison-op

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `SamplerComparisonState` declaration that, across all reflection-visible call sites against textures bound with this sampler, is only used with non-`Cmp`-suffixed sample methods (`Sample`, `SampleLevel`, `SampleGrad`, `SampleBias`) and never with the comparison-suffixed variants (`SampleCmp`, `SampleCmpLevelZero`, `SampleCmpLevel`, `GatherCmp`). The detector enumerates sampler bindings via reflection, finds every `Sample*` call against textures that reference each comparison sampler, and fires when no call site uses a comparison method. It does not fire when at least one call site uses a `Cmp` variant against the sampler.

## Why it matters on a GPU

`SamplerComparisonState` is a distinct descriptor type from `SamplerState` on every modern API and IHV. It carries a `ComparisonFunc` field (`LESS`, `LESS_EQUAL`, `GREATER`, etc.) that drives a hardware comparison-and-blend path inside the sampler unit. On AMD RDNA 2/3 the TMU has a dedicated PCF (percentage-closer filtering) hardware path that, given a comparison sampler, performs four texel comparisons against the reference value and returns a blended in-shadow / out-of-shadow ratio in one sampler-unit cycle. NVIDIA Turing/Ada and Intel Xe-HPG document equivalent comparison hardware. The descriptor's `ComparisonFunc` field is *only* consumed by the `SampleCmp*` and `GatherCmp*` methods; non-`Cmp` calls ignore the field entirely.

When a comparison sampler is bound but only non-`Cmp` calls are made, the descriptor occupies a sampler heap slot that could have been a regular `SamplerState`, the `ComparisonFunc` value is dead state in the descriptor, and the PCF path of the sampler unit is unused. Worse, the pattern is misleading to readers: a `SamplerComparisonState` named `ShadowSampler` strongly implies the shader does PCF shadow filtering. A reader who later adds a real shadow-filtering call site might assume the sampler's `ComparisonFunc` is set appropriately for shadows, when in fact the field has been ignored across the existing call sites and may not be set at all.

The fix is one of: switch the declaration to plain `SamplerState`, or change the call sites to use `SampleCmp` / `SampleCmpLevelZero` against a depth/shadow texture and consume the comparison result. The rule does not assume which one is intended; it surfaces the mismatch.

## Examples

### Bad

```hlsl
SamplerComparisonState ShadowSampler : register(s0);
Texture2D<float>       ShadowMap     : register(t0);

float read_shadow(float2 uv) {
    // Non-Cmp call on a comparison sampler — descriptor is wrong type
    // for the call, ComparisonFunc is unused.
    return ShadowMap.Sample(ShadowSampler, uv);
}
```

### Good

```hlsl
SamplerComparisonState ShadowSampler : register(s0);
Texture2D<float>       ShadowMap     : register(t0);

float read_shadow(float3 uv_and_z) {
    // Hardware PCF: ShadowMap.SampleCmp returns the comparison-blended
    // result in one TMU cycle on RDNA 2/3 / Turing / Ada.
    return ShadowMap.SampleCmpLevelZero(ShadowSampler, uv_and_z.xy, uv_and_z.z);
}

// Or, if the call sites really only need plain sampling, change the
// declaration:
SamplerState PlainSampler : register(s0);
```

## Options

none

## Fix availability

**suggestion** — Both directions of the fix have semantic implications (changing the descriptor type changes the root signature; changing the call site changes the math). The diagnostic identifies the mismatch; the author chooses the resolution.

## See also

- Related rule: [static-sampler-when-dynamic-used](static-sampler-when-dynamic-used.md) — sampler descriptor placement
- Related rule: [anisotropy-without-anisotropic-filter](anisotropy-without-anisotropic-filter.md) — sampler descriptor field that has no effect at the call site
- HLSL intrinsic reference: `SampleCmp`, `SampleCmpLevelZero`, `SamplerComparisonState` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/comparison-sampler-without-comparison-op.md)

*© 2026 NelCit, CC-BY-4.0.*
