---
id: missing-precise-on-pcf
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# missing-precise-on-pcf

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Depth-compare arithmetic in a pixel/compute shader — typically the receiver-side computation that feeds a `SampleCmp`, `SampleCmpLevelZero`, or `GatherCmp` call against a shadow-map texture, or a manual PCF (percentage-closer filtering) kernel that compares a computed receiver depth against several sampled shadow-map depths and accumulates the comparisons into a softness factor. The rule fires on the receiver-depth expression and the per-tap comparison expressions when none of the contributing locals are marked `precise`, when the shadow projection involves a perspective divide (`coord.xyz / coord.w`) or a depth-bias term added to the receiver depth, and when the result feeds a comparison sampler or an `if (recv < occluder)` test. The diagnostic suggests adding `precise` to the receiver-depth local and to any intermediate that participates in the perspective divide or the bias add.

## Why it matters on a GPU

HLSL gives the compiler wide latitude to reorder, fuse, and re-associate floating-point arithmetic. `(a * b) + c` may be lowered as a fused multiply-add (FMA) or as a separate multiply followed by an add; `(a + b) + c` may be evaluated as `a + (b + c)`; a perspective divide `x / w` may be lowered as `x * rcp(w)` with a single-Newton-step `rcp` whose error envelope differs by one or two ULPs from a true IEEE divide. Each of these choices is, in isolation, well within the IEEE-754 tolerance the spec promises — but the choice differs between vendors and between driver versions. AMD's RDNA shader compiler aggressively forms FMAs and prefers `v_rcp_f32` over IEEE divide; NVIDIA's Turing/Ada compiler also forms FMAs but with different operand-ordering heuristics; Intel Xe-HPG's IGC has its own pass ordering. The result: the same HLSL receiver-depth expression produces depths that disagree at the last 1-2 ULPs across IHVs.

For most arithmetic, last-ULP disagreement is invisible. For PCF shadow filtering, it is catastrophic at primitive edges. The shadow-map comparison is `step(receiver_depth, occluder_depth)` — a hard binary test that flips the moment receiver crosses occluder. If receiver and occluder are nominally equal at a pixel (the surface is exactly the casting surface), one IHV's reordering can push receiver below occluder and produce "lit", while another pushes it above and produces "shadowed". Across a full screen of self-shadowing surfaces, this manifests as the classic "shadow acne" pattern — but instead of being a uniform pattern that depth bias can compensate for, it is a per-IHV checkerboard that breaks the bias calibration. On RDNA 2 in particular, the aggressive FMA formation in the receiver-depth pass tends to bias receiver downward by ~1 ULP relative to the same expression on NVIDIA Ada, producing visible shadow-edge flicker when the same shader is shipped to both vendors.

The `precise` qualifier disables algebraic reordering and FMA contraction for any arithmetic that flows into a `precise`-qualified destination. Marking the receiver-depth local `precise float recv = shadowCoord.z / shadowCoord.w + bias;` forces the compiler to evaluate the divide and the add in source order, with no FMA contraction, on every backend. The cost is a handful of extra instructions per pixel — `precise` typically costs one or two additional VALU slots compared to the FMA-contracted form, and the divide cannot be fused with the bias add — but the visual stability is worth orders of magnitude more than the throughput. For a 4-tap or 16-tap PCF kernel, the bottleneck is the dependent shadow-map sample latency, not the receiver-depth arithmetic, so the `precise` cost is effectively hidden under sampler latency.

## Examples

### Bad

```hlsl
Texture2D<float>          ShadowMap   : register(t0);
SamplerComparisonState    ShadowSamp  : register(s0);

float SampleShadow(float4 shadowCoord, float bias) {
    // Compiler is free to FMA, reorder, and reassociate the divide/add chain.
    // Two vendors will produce two different last-ULP receiver depths.
    float recv = shadowCoord.z / shadowCoord.w + bias;

    float sum = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        float2 uv = shadowCoord.xy / shadowCoord.w + float2(dx, dy) * (1.0 / 2048.0);
        sum += ShadowMap.SampleCmp(ShadowSamp, uv, recv);
    }
    return sum / 9.0;
}
```

### Good

```hlsl
float SampleShadow(float4 shadowCoord, float bias) {
    // 'precise' forces strict left-to-right evaluation: divide first, then add.
    // No FMA contraction; identical bit pattern across AMD, NVIDIA, Intel.
    precise float invW = 1.0 / shadowCoord.w;
    precise float recv = shadowCoord.z * invW + bias;
    precise float2 baseUV = shadowCoord.xy * invW;

    float sum = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx) {
        float2 uv = baseUV + float2(dx, dy) * (1.0 / 2048.0);
        sum += ShadowMap.SampleCmp(ShadowSamp, uv, recv);
    }
    return sum / 9.0;
}
```

## Options

none

## Fix availability

**suggestion** — Adding `precise` is conservative: it cannot make a numerically-correct shader incorrect, but it does change the generated instruction sequence and may surface latent dependencies that the FMA-contracted form was hiding. The author should verify that the new sequence still meets the perf budget for the shadow pass (typically a non-issue: PCF is sampler-bound, not ALU-bound) and that the bias term, if any, is recalibrated to the now-deterministic receiver depth. The diagnostic shows the proposed edit but does not apply it automatically.

## See also

- Related rule: [`shadow-bias-too-small`](shadow-bias-too-small.md) — receiver-depth bias below the per-IHV ULP envelope
- Related rule: [`samplecmp-without-comparison-sampler`](samplecmp-without-comparison-sampler.md) — `SampleCmp` called with a non-comparison sampler
- HLSL reference: `precise` qualifier in the DirectX HLSL Language Reference
- D3D Functional Spec: `SampleCmp`, `SampleCmpLevelZero`, comparison-sampler behaviour
- Companion blog post: _not yet published — will appear alongside the v0.3.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/missing-precise-on-pcf.md)

*© 2026 NelCit, CC-BY-4.0.*
