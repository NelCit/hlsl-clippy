---
id: texture-lod-bias-without-grad
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# texture-lod-bias-without-grad

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

Calls to `SampleBias(sampler, uv, bias)` in any of these contexts: a compute shader (any function decorated with `[numthreads]`), a function that does not execute in a pixel-shader quad (detected via Slang stage reflection), or a pixel shader function body where the UV argument is not quad-uniform (for example, when `uv` is computed inside a non-uniform branch that diverges within a 2x2 quad). `SampleBias` adds a floating-point offset to the hardware-computed LOD before selecting the mip level. The LOD itself is computed from the implicit derivatives `ddx(uv)` and `ddy(uv)`, which are only defined in pixel shader stage within a fully converged 2x2 quad. The rule does not fire in pixel shader entry points where the UV is demonstrably quad-uniform (e.g., derived from a linear interpolator with no per-lane divergence before the call).

## Why it matters on a GPU

Implicit derivatives — the values returned by `ddx()` and `ddy()` — are computed by the hardware using the difference between the current lane's value and its horizontal or vertical neighbour lane within a 2x2 pixel quad. The quad is the fundamental unit of pixel shader execution on all current GPU architectures: AMD GCN through RDNA 3, NVIDIA Kepler through Ada Lovelace, and Intel Xe-HPG all dispatch pixel shaders in 2x2 quads to support finite-difference derivative computation. In compute shaders, no such quad structure exists — the notion of a neighbouring lane in pixel space is undefined. When `ddx(uv)` is evaluated in a compute shader, the compiler either inserts a zero (producing a derivative of zero, which maps to mip 0) or produces architecturally undefined results, depending on the driver and shader model version.

`SampleBias` is defined in terms of the implicit LOD: `LOD_biased = LOD_implicit + bias`. If `LOD_implicit` is undefined or zero in a compute context, the bias term is either silently ignored or added to an incorrect base, producing a LOD that differs from the author's intent without any compiler warning. The most common failure mode is that `SampleBias` in compute collapses to effectively `SampleLevel(s, uv, bias)` with `LOD_implicit = 0`, which means the bias itself becomes the absolute mip level. A bias of `+1.0` intended to add one mip level of blur instead selects mip 1 regardless of the on-screen footprint — producing incorrect results on close-up geometry.

In non-quad-uniform control flow within a pixel shader, the same hazard applies. If a pixel shader takes a branch that is not uniform across all four lanes in the 2x2 quad, derivative computation inside that branch is undefined for the lanes that did not take the branch (they execute the branch speculatively as helper lanes, which may or may not produce correct derivative values depending on the driver's helper-lane suppression policy). `SampleBias` in a divergent branch is therefore a correctness risk. The rule reports the specific shader stage and the reason the call site is flagged; the suggested fix is to replace `SampleBias` with `SampleLevel` using an explicitly computed LOD, or to move the call to a point in the pixel shader where quad-uniformity is guaranteed.

## Examples

### Bad

```hlsl
// SampleBias in a compute shader — implicit derivatives are undefined.
Texture2D<float4> EnvMap    : register(t0);
SamplerState      LinearClamp : register(s0);

[numthreads(8, 8, 1)]
void cs_env_prefilter(uint3 dtid : SV_DispatchThreadID) {
    float2 uv = (float2(dtid.xy) + 0.5) * InvResolution;
    // HIT(texture-lod-bias-without-grad): SampleBias in compute — implicit
    // derivatives are undefined; LOD_implicit = 0 so bias becomes absolute mip.
    float4 env = EnvMap.SampleBias(LinearClamp, uv, 1.0);
    Output[dtid.xy] = env;
}

// SampleBias inside a non-quad-uniform branch in a pixel shader.
float4 ps_conditional(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                      nointerpolation uint matId : TEXCOORD1) : SV_Target {
    float4 result = 0;
    if (matId == 1u) {
        // HIT(texture-lod-bias-without-grad): branch diverges per-lane (matId is
        // nointerpolation uint) — derivatives inside this branch are undefined.
        result = EnvMap.SampleBias(LinearClamp, uv, -0.5);
    }
    return result;
}
```

### Good

```hlsl
// Compute shader: use SampleLevel with an explicitly computed or passed LOD.
[numthreads(8, 8, 1)]
void cs_env_prefilter(uint3 dtid : SV_DispatchThreadID) {
    float2 uv  = (float2(dtid.xy) + 0.5) * InvResolution;
    float  lod = TargetMipLevel;   // push constant or cbuffer field
    float4 env = EnvMap.SampleLevel(LinearClamp, uv, lod);
    Output[dtid.xy] = env;
}

// Pixel shader: compute the bias-adjusted LOD using CalculateLevelOfDetail
// before entering divergent control flow.
float4 ps_conditional(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                      nointerpolation uint matId : TEXCOORD1) : SV_Target {
    // Compute LOD outside the divergent branch where derivatives are defined.
    float baseLod = EnvMap.CalculateLevelOfDetail(LinearClamp, uv);
    float4 result = 0;
    if (matId == 1u) {
        result = EnvMap.SampleLevel(LinearClamp, uv, baseLod - 0.5);
    }
    return result;
}
```

## Options

none

## Fix availability

**suggestion** — The rule can propose replacing `SampleBias(s, uv, bias)` with `SampleLevel(s, uv, CalculateLevelOfDetail(s, uv) + bias)` when the call site is inside a pixel shader and derivatives can be moved to a quad-uniform point. In compute shaders, no automatic LOD can be computed and the fix is instead to replace with `SampleLevel(s, uv, explicitLod)` using a value the author must supply. `hlsl-clippy fix` shows the candidate edit but requires the author to supply or verify the explicit LOD expression.

## See also

- Related rule: [`samplegrad-with-constant-grads`](samplegrad-with-constant-grads.md) — zero explicit gradients collapse to mip 0 similarly
- Related rule: [`samplelevel-with-zero-on-mipped-tex`](samplelevel-with-zero-on-mipped-tex.md) — explicit mip 0 on mipped resources
- HLSL intrinsic reference: `Texture2D.SampleBias`, `Texture2D.SampleLevel`, `Texture2D.CalculateLevelOfDetail` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/texture-lod-bias-without-grad.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
