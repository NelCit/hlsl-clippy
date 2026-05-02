---
id: gather-channel-narrowing
category: texture
severity: note
applicability: machine-applicable
since-version: "v0.3.0"
phase: 3
---

# gather-channel-narrowing

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

Expressions of the form `texture.Gather(sampler, uv).r`, `.g`, `.b`, or `.a` — where `Gather` is called and only a single scalar channel of the resulting `float4` is consumed. The rule fires when the swizzle immediately follows the `Gather` call and the remaining three components are provably dead (not stored, not passed to a function, not part of a larger swizzle). The equivalent hardware-direct call is `GatherRed`, `GatherGreen`, `GatherBlue`, or `GatherAlpha` respectively. The rule fires on `Texture2D`, `Texture2DArray`, and `TextureCube` gather variants. It does not fire when more than one channel of the gathered `float4` is used downstream.

## Why it matters on a GPU

`Gather` is a TMU operation that fetches the four texels in the 2x2 bilinear footprint surrounding a UV coordinate and returns one scalar channel from each texel packed into a `float4`. On all current GPU architectures (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG), a single `Gather` instruction completes in one TMU issue cycle regardless of which channel it returns. The hardware has dedicated `GatherRed`, `GatherGreen`, `GatherBlue`, and `GatherAlpha` instruction variants that select the channel at the TMU instruction level, not in a post-processing step.

When a shader writes `texture.Gather(sampler, uv).r`, the compiler must emit a `Gather` instruction and then extract the `.r` component. On most shader compilers targeting DXIL SM6, this lowering produces the correct `image_gather4_r` (RDNA) or `GATHER4_PO` with a channel-select field (DXIL). However, the mapping depends on the shader compiler's optimisation level and the specific DXC or FXC version. Using the explicit `GatherRed(sampler, uv)` form guarantees the correct single-channel instruction without relying on the compiler to recognise the narrowing pattern, and it expresses intent unambiguously to human readers.

There is also a readability benefit: `GatherRed(sampler, uv)` returns a `float4` of four red-channel values, which is semantically clearer than `Gather(sampler, uv).r` — the latter requires the reader to remember that `.r` here means "the red channel of each of the four gathered texels", not "gather all channels and take the first". The fix is mechanical and always safe: `GatherRed`, `GatherGreen`, `GatherBlue`, and `GatherAlpha` are available on all Shader Model 4.1+ targets (D3D_FEATURE_LEVEL_10_1 and above), which covers all platforms targeted by Phase 3 rules.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/textures.hlsl, line 20
// HIT(gather-channel-narrowing): only the .r channel is used → GatherRed.
Texture2D    BaseColor : register(t0);
SamplerState Bilinear  : register(s0);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float r = BaseColor.Gather(Bilinear, uv).r;
    // ...
}

// From tests/fixtures/phase3/textures_extra.hlsl, lines 23-26
// HIT(gather-channel-narrowing): only .g channel used → GatherGreen.
float4 sample_roughness_gather(float2 uv) {
    return float4(Roughness.Gather(LinearWrap, uv).g, 0, 0, 1);
}
```

### Good

```hlsl
// After machine-applicable fix: explicit GatherRed — one TMU instruction,
// no ambiguity, no reliance on compiler channel-narrowing optimisation.
float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float r = BaseColor.GatherRed(Bilinear, uv).r;
    // ...
}

float4 sample_roughness_gather(float2 uv) {
    return float4(Roughness.GatherGreen(LinearWrap, uv).r, 0, 0, 1);
}

// When all four gathered values are used, Gather is correct and the rule does not fire:
// SHOULD-NOT-HIT(gather-channel-narrowing)
float4 sample_roughness_gather_all(float2 uv) {
    float4 g = Roughness.Gather(LinearWrap, uv);
    return g.xyzw;   // all four channels consumed
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `texture.Gather(s, uv).r` with `texture.GatherRed(s, uv).r` (and analogously for `.g` → `GatherGreen`, `.b` → `GatherBlue`, `.a` → `GatherAlpha`) is a pure textual substitution. The instruction selected by the hardware is identical in both cases on all SM4.1+ targets. `hlsl-clippy fix` applies it without human confirmation.

## See also

- Related rule: [`gather-cmp-vs-manual-pcf`](gather-cmp-vs-manual-pcf.md) — shadow-map PCF using `GatherCmp` instead of multiple `SampleCmp` calls
- HLSL intrinsic reference: `Texture2D.Gather`, `Texture2D.GatherRed`, `Texture2D.GatherGreen`, `Texture2D.GatherBlue`, `Texture2D.GatherAlpha` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/gather-channel-narrowing.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
