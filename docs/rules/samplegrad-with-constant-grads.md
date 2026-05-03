---
id: samplegrad-with-constant-grads
category: texture
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# samplegrad-with-constant-grads

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

Calls to `SampleGrad(sampler, uv, ddx, ddy)` where both the `ddx` and `ddy` arguments are constant zero — either as `float2(0, 0)`, `float2(0.0, 0.0)`, `(float2)0`, or any expression that evaluates to a zero vector at compile time. The rule fires regardless of the texture type (`Texture2D`, `TextureCube`, `Texture2DArray`, etc.) and regardless of the UV dimensionality (`float2`, `float3`). It does not fire when either gradient argument is non-zero or when either argument is a runtime expression.

## Why it matters on a GPU

`SampleGrad` is the explicit-gradient variant of `Sample`. It accepts caller-supplied partial derivatives (ddx and ddy) so that the hardware LOD calculation uses those derivatives instead of computing them from the implicit 2x2 quad footprint. This is the right tool when derivatives are known analytically — for example, in a compute shader, inside a non-uniform control-flow block, or when sampling with custom UV transformations. The hardware TMU receives the gradient pair and computes `LOD = log2(max(length(ddx), length(ddy)))` to determine which mip level to sample.

When both gradients are zero, `length(float2(0,0))` is zero, and `log2(0)` is negative infinity. The hardware clamps this to the minimum LOD, which is mip 0. The result is exactly identical to calling `SampleLevel(sampler, uv, 0)`. The difference is performance: on all current GPU families (AMD RDNA 2/3, NVIDIA Turing/Ada, Intel Xe-HPG), `SampleGrad` requires the TMU to accept and process two additional gradient registers per instruction. In a tight sampling loop this adds two extra source register reads per instruction and may increase register pressure enough to lower occupancy by one wave per CU/SM. `SampleLevel` with an explicit zero encodes the same semantic intent with a scalar LOD argument, halving the per-instruction register cost for the LOD term.

The replacement from `SampleGrad(s, uv, float2(0,0), float2(0,0))` to `SampleLevel(s, uv, 0)` is a pure semantic equivalence: the hardware behaviour at runtime is identical (mip 0 is fetched in both cases), the fix is mechanical and cannot produce a regression, and `shader-clippy fix` applies it automatically. Authors who intentionally want mip 0 from a non-pixel-shader context should prefer `SampleLevel` directly — it is both more efficient and more readable in conveying intent.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/textures.hlsl, line 23
// HIT(samplegrad-with-constant-grads): zero gradients = SampleLevel(0).
Texture2D    BaseColor : register(t0);
SamplerState Bilinear  : register(s0);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c = BaseColor.SampleGrad(Bilinear, uv, float2(0, 0), float2(0, 0)).rgb;
    // ...
}
```

### Good

```hlsl
// After machine-applicable fix: SampleLevel(s, uv, 0) is semantically identical
// and avoids the gradient register overhead.
float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c = BaseColor.SampleLevel(Bilinear, uv, 0).rgb;
    // ...
}

// If the intent is to restore automatic LOD in a pixel shader, use Sample instead:
float4 entry_main_pixel(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 c = BaseColor.Sample(Bilinear, uv).rgb;
    // ...
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `SampleGrad(s, uv, float2(0,0), float2(0,0))` with `SampleLevel(s, uv, 0)` is a pure textual substitution. The LOD selected by both calls is identical: mip 0. `shader-clippy fix` applies it without human confirmation.

## See also

- Related rule: [`samplelevel-with-zero-on-mipped-tex`](samplelevel-with-zero-on-mipped-tex.md) — the output of this fix may itself trigger this rule if the target resource is mipped
- HLSL intrinsic reference: `Texture2D.SampleGrad`, `Texture2D.SampleLevel` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/samplegrad-with-constant-grads.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
