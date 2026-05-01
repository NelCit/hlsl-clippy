---
id: wave-reduction-pixel-without-helper-attribute
category: wave-helper-lane
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# wave-reduction-pixel-without-helper-attribute

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A pixel-shader entry that performs a wave reduction (`WaveActiveSum`, `WaveActiveProduct`, `WaveActiveCountBits`, `WaveActiveBallot`, etc.) whose result then flows into a derivative-bearing operation (`ddx`, `ddy`, an implicit-derivative texture sample) without `[WaveOpsIncludeHelperLanes]` declared on the entry. The Phase 4 data-flow analysis traces the reduction's output to the derivative operation; the rule fires when both occur and the attribute is absent.

## Why it matters on a GPU

By default, pixel-shader wave intrinsics exclude helper lanes from the active mask: a `WaveActiveSum(x)` that sees a partially-covered quad sums only the covered lanes and ignores the helpers. This is usually what the author wants — helpers don't contribute meaningful values for non-derivative work. But when the reduction's result then flows into a derivative operation, the derivative computation needs the full quad to be coherent: `ddx(uniform)` is zero only when *all four* quad lanes have the same value.

If the helper lanes saw a different reduction result than the covered lanes (because they were excluded from the reduction), the derivative is contaminated. On NVIDIA Turing/Ada Lovelace and AMD RDNA 2/3 the contamination produces wrong texture-sample mip levels (because the derivative drives mip selection); on Intel Xe-HPG the same effect appears.

`[WaveOpsIncludeHelperLanes]` (SM 6.7) opts the entry into including helpers in wave intrinsics. When the reduction's result reaches a derivative, the attribute is required for correctness. The Phase 4 data-flow rule catches the missing-attribute case.

The fix is to add the attribute to the entry. The rule is suggestion-tier because in some kernels the derivative is intentionally allowed to be approximate (e.g., a debug visualisation); the diagnostic emits the candidate attribute as a comment.

## Examples

### Bad

```hlsl
// PS: reduction excludes helpers; derivative on the result is contaminated.
Texture2D<float4> g_Albedo : register(t0);
SamplerState      g_Sampler : register(s0);

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x) / WaveActiveCountBits(true);
    float dudx    = ddx(waveAvg);     // helper lanes had wrong waveAvg
    return g_Albedo.Sample(g_Sampler, uv + float2(dudx, 0));
}
```

### Good

```hlsl
[WaveOpsIncludeHelperLanes]
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x) / WaveActiveCountBits(true);
    float dudx    = ddx(waveAvg);
    return g_Albedo.Sample(g_Sampler, uv + float2(dudx, 0));
}
```

## Options

none

## Fix availability

**suggestion** — Adding the attribute is a one-line change but requires confirming that the helper-lane values are well-defined for the reduction. The diagnostic emits the candidate as a comment.

## See also

- Related rule: [waveops-include-helper-lanes-on-non-pixel](waveops-include-helper-lanes-on-non-pixel.md) — opposite-direction stage rule
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — companion helper-lane rule
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — companion derivative-correctness rule
- HLSL specification: [SM 6.7 WaveOpsIncludeHelperLanes attribute](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_7.html)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wave-reduction-pixel-without-helper-attribute.md)

*© 2026 NelCit, CC-BY-4.0.*
