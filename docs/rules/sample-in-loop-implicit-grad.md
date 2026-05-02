---
id: sample-in-loop-implicit-grad
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# sample-in-loop-implicit-grad

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `Texture2D::Sample`, `Texture2DArray::Sample`, `TextureCube::Sample`, and the other `Sample` overloads that compute texture LOD from implicit screen-space derivatives, when the call appears inside a loop, inside a branch whose condition is not provably uniform across the pixel quad, or inside a non-inlined function whose call sites mix uniform and non-uniform contexts. The same pattern applies to `SampleBias` and `SampleCmp` because both still rely on implicit `ddx`/`ddy` of the texture coordinate. The rule does not fire on `SampleLevel`, `SampleGrad`, or `Load`, which carry their own LOD information and do not depend on cross-lane derivatives.

## Why it matters on a GPU

Pixel shaders execute as 2x2 quads — the smallest unit at which the rasterizer guarantees neighbouring fragments are co-resident on the same SIMD lanes. Implicit-derivative texture sampling computes mip selection by differencing the texture coordinate against the three other lanes in the quad. On AMD RDNA 2/3, this is implemented by the `image_sample` instruction reading `S#` and `T#` operands across the four quad lanes through the cross-lane permute network. On NVIDIA Turing and Ada, the texture unit (TMU) consumes coordinates from all four quad lanes in parallel and forms the partial derivatives in dedicated derivative-computation hardware before issuing the actual fetch. On Intel Xe-HPG, the same quad-coupled fetch protocol applies through the sampler subsystem.

When the four quad lanes do not agree on whether to execute the `Sample` call — because the call sits inside a divergent `if`, or because a loop has a different trip count per pixel, or because one quad lane took an early return — the derivative computation reads coordinate values from inactive lanes. The hardware does not abort: it returns whatever bit pattern the masked-off lanes hold, which can be a stale value from a previous instruction, garbage from helper-lane initialisation, or in some drivers a deliberately-poisoned NaN. The downstream mip selection is then wrong by an unbounded factor, producing speckle, shimmer, or full-frame stippling artefacts that vary by shader compiler version. The D3D12 specification labels this case as undefined behaviour for implicit-derivative samples.

The fix is to use `SampleLevel(s, uv, mip)` with an explicit mip level (often `0` for UI / post / compute-style passes), or `SampleGrad(s, uv, ddx_uv, ddy_uv)` with derivatives computed in uniform control flow before the divergent region. For loops where the coordinate evolves per iteration, hoist a `SampleGrad` outside the loop using the loop-invariant gradient, or restructure the loop so the sample executes in uniform control flow with the loop variable folded into an explicit LOD via `log2` of a step magnitude.

## Examples

### Bad

```hlsl
Texture2D    Albedo       : register(t0);
SamplerState LinearSampler : register(s0);

float4 ps_loop_sample(float2 uv : TEXCOORD0, uint count : COLOR0) : SV_Target {
    float4 acc = 0;
    for (uint i = 0; i < count; ++i) {
        // 'count' varies per pixel; quad lanes leave the loop at different
        // iterations. Implicit derivatives become undefined.
        acc += Albedo.Sample(LinearSampler, uv + float2(i, 0) * 0.01);
    }
    return acc / max(1u, count);
}
```

### Good

```hlsl
float4 ps_loop_sample_fixed(float2 uv : TEXCOORD0, uint count : COLOR0) : SV_Target {
    // Compute gradients once in uniform CF (outside the loop) and pass
    // them explicitly so each iteration is independent of quad neighbours.
    float2 ddx_uv = ddx(uv);
    float2 ddy_uv = ddy(uv);
    float4 acc = 0;
    for (uint i = 0; i < count; ++i) {
        acc += Albedo.SampleGrad(LinearSampler, uv + float2(i, 0) * 0.01,
                                 ddx_uv, ddy_uv);
    }
    return acc / max(1u, count);
}
```

## Options

none

## Fix availability

**suggestion** — A candidate rewrite to `SampleGrad` is shown but requires verification: the appropriate gradient depends on how the sampling coordinate evolves in the divergent region. A blanket textual replacement would change mip selection semantically. The diagnostic identifies the `Sample` call and the enclosing non-uniform construct.

## See also

- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — explicit `ddx`/`ddy` in divergent CF
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — quad-lane helper participation hazards
- HLSL intrinsic reference: `Sample`, `SampleLevel`, `SampleGrad` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/sample-in-loop-implicit-grad.md)

*© 2026 NelCit, CC-BY-4.0.*
