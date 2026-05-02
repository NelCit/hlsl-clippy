---
id: samplecmp-vs-manual-compare
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# samplecmp-vs-manual-compare

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

A pattern where a shader samples a depth or shadow texture with a standard `Sample` or `Load` call and then performs a scalar comparison (`<`, `>`, `<=`, `>=`) on the fetched depth value against a reference depth variable. The rule fires when both conditions hold: the sampled texture is a declared depth resource (reflection type `Texture2D<float>` or `Texture2D<float4>` bound as a shadow map), and the comparison result drives a branch or is used as a shadow weight without passing through `SampleCmp`. It does not fire on uses of `SampleCmp` or `SampleCmpLevelZero` directly, nor when the comparison is against a constant rather than a per-pixel reference depth.

## Why it matters on a GPU

The TMU on all current GPU architectures (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG) includes a dedicated hardware comparison unit on the texture pipeline. `SampleCmp` with a `SamplerComparisonState` routes the reference depth into this unit, which performs a bilinear comparison across the 2x2 texel footprint in a single TMU issue. On RDNA 3, a `SampleCmp` completes in the same number of TMU cycles as a plain `Sample` for many formats, because the comparison is fused into the filter stage at no additional ALU cost. The output is a single float in [0, 1] representing the filtered shadow term.

A hand-rolled equivalent — `Sample(s, uv).r < refDepth ? 0.0 : 1.0` — fetches one texel at one mip level without any bilinear filtering in the comparison. It produces a hard binary shadow edge rather than a filtered percentage-closer shadow, because the comparison is performed on a single fetched value rather than across the 2x2 bilinear footprint. To replicate the hardware PCF quality of `SampleCmp`, the author would need to sample the four neighbours manually, which costs four TMU operations at one cycle each plus four scalar compares, versus the single TMU operation of `SampleCmp`. On RDNA 3, the TMU issue rate for `SampleCmp` on a D32_SFLOAT shadow sampler is the same as for `Sample` — typically one result per cycle per TMU — whereas four sequential `Sample` calls cost four cycles minimum.

Beyond performance, the `SamplerComparisonState` carries GPU-driver-level tuning for the comparison function (depth function and filter mode), which driver-side depth-bias corrections can hook into for shadow-map quality improvements. Using a manual comparison entirely bypasses this hardware path and may produce visible shadow acne at geometry silhouettes. The rule is a `suggestion` because replacing `Sample` with `SampleCmp` also requires swapping the `SamplerState` binding for a `SamplerComparisonState` binding, a change that propagates to the root signature and CPU-side descriptor creation.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/textures.hlsl, lines 27-30
// HIT(samplecmp-vs-manual-compare): hand-rolled depth compare. Use SampleCmp.
Texture2D              ShadowMap : register(t3);
SamplerState           Bilinear  : register(s0);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float refDepth    = pos.z;
    float shadowSample = ShadowMap.Sample(Bilinear, uv).r;
    float shadow       = shadowSample < refDepth ? 1.0 : 0.0;
    // ...
}

// From tests/fixtures/phase3/textures_extra.hlsl, lines 36-41
// HIT(samplecmp-vs-manual-compare): hand-rolled depth test on ShadowAtlas.
float pcf_manual(float2 uv, float refDepth) {
    float smpl = ShadowAtlas.Sample(LinearWrap, uv).r;
    return smpl < (refDepth - ShadowBias) ? 0.0 : 1.0;
}
```

### Good

```hlsl
// After fix: SampleCmp with a SamplerComparisonState performs hardware PCF
// across the 2x2 bilinear footprint in a single TMU operation.
Texture2D              ShadowMap : register(t3);
SamplerComparisonState ShadowCmp : register(s1);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float refDepth = pos.z;
    float shadow   = ShadowMap.SampleCmp(ShadowCmp, uv, refDepth);
    // ...
}

// With bias folded into the reference value:
float pcf_correct(float2 uv, float refDepth) {
    return ShadowAtlas.SampleCmp(CmpSampler, uv, refDepth - ShadowBias);
}
```

## Options

none

## Fix availability

**suggestion** — The rule proposes replacing `Sample(s, uv).r < ref` with `SampleCmp(cmpSampler, uv, ref)`. Because this change requires swapping the `SamplerState` binding for a `SamplerComparisonState` and updating the root signature on the CPU side, `hlsl-clippy fix` shows a candidate edit but does not apply it automatically. Verify the sampler binding before accepting the suggestion.

## See also

- Related rule: [`gather-cmp-vs-manual-pcf`](gather-cmp-vs-manual-pcf.md) — for 2x2 unrolled `SampleCmp` patterns, `GatherCmp` further reduces TMU calls
- Related rule: [`missing-precise-on-pcf`](missing-precise-on-pcf.md) — depth-compare arithmetic without `precise` is a shadow acne risk
- HLSL intrinsic reference: `Texture2D.SampleCmp`, `Texture2D.SampleCmpLevelZero`, `SamplerComparisonState` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/samplecmp-vs-manual-compare.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
