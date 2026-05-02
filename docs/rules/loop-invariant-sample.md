---
id: loop-invariant-sample
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# loop-invariant-sample

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to any texture sampling intrinsic — `Sample`, `SampleLevel`, `SampleGrad`, `SampleBias`, `SampleCmp`, `SampleCmpLevelZero`, `Gather`, `GatherRed`, `GatherGreen`, `GatherBlue`, `GatherAlpha`, `Load` — inside a loop body when the texture argument, sampler argument, and all coordinate arguments are loop-invariant: none of them depend on the loop induction variable or any value defined inside the loop. The rule fires when the UV or load coordinate is determined by the data-flow graph to be loop-invariant (no transitive dependency on the loop counter or any variable assigned inside the loop). It does not fire when any argument — including the mip level for `SampleLevel`, the gradient for `SampleGrad`, or any component of the coordinate — varies with the loop counter.

## Why it matters on a GPU

A texture sample is one of the most expensive single operations in a GPU shader, not in ALU cycles but in latency and Texture Memory Unit (TMU) traffic. On AMD RDNA 3 and NVIDIA Ada Lovelace, an `SampleLevel` call with a resident mip costs approximately 100-300 clock cycles of TMU latency (depending on cache state), though this latency can be hidden by the scheduler issuing other independent instructions. If the sample is loop-invariant, issuing it N times across N iterations costs N times the TMU bandwidth and N cache-fill attempts. There is no TMU-level CSE (common subexpression elimination) across loop iterations; each call issues a fresh request. The GPU compiler may decline to hoist the sample out of the loop because it cannot prove that the texture contents have not changed between iterations (aliasing analysis for GPU textures is conservative), or because the VGPR pressure required to hold the result live across iterations is judged higher than the bandwidth saving.

When the programmer adds the sample result to a loop accumulator using a loop-invariant scale factor, the sample-per-iteration pattern is especially wasteful: the identical texel is fetched N times, scaled by the same constant, and added N times — equivalent to fetching once and multiplying by `N * scale`. Even in the case where the sample result is not being accumulated, the compiler's loop-invariant code motion (LICM) pass may choose not to hoist the sample because TMU calls have hidden side effects from the compiler's perspective (sampler-feedback write-out, tiled-resource residency updates). Explicit hoisting by the programmer removes the ambiguity and eliminates the redundant TMU requests unconditionally.

On systems where the L1 texture cache is small (512 KB on RDNA 3 per shader array, shared among all waves), repeated identical sample requests in a loop can evict other useful cache lines between iterations if other work interleaves. Even if the first sample hits the L2 (8-64 MB on RDNA 3), subsequent identical requests still pay the L2 round-trip latency per iteration. Moving the sample before the loop guarantees exactly one TMU request and one L1 fill, with the result residing in a VGPR for the duration of the loop body — a cost of 4 bytes per lane per float4, well within the VGPR file capacity for small loops.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/loop_invariant.hlsl, line 13-21
// HIT(loop-invariant-sample): UV does not depend on the loop counter;
// the sample is constant across all iterations and should be hoisted.
float4 ps_loop_invariant_sample(float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        // 'Center' is a cbuffer field — loop-invariant.
        // This Sample call issues 16 identical TMU requests.
        acc += Tex.Sample(Bilinear, Center) * (1.0 / 16.0);
    }
    return acc;
}

// From tests/fixtures/phase4/loop_invariant_extra.hlsl, line 27-39
// HIT(loop-invariant-sample): center_uv never changes inside the loop — the
// Sample result is constant and should be computed once before the loop.
float4 blur_with_invariant_center(float2 uv) {
    float2 center_uv = uv;
    float4 acc = 0;
    float  wsum = 0;
    for (uint i = 0; i < TapCount; ++i) {
        float  w = exp(-(float)i * (float)i / (2.0 * Sigma * Sigma));
        float2 offset = Direction * (float)i * InvRes;
        acc  += BlurTex.SampleLevel(Linear, uv + offset, 0) * w;
        // HIT(loop-invariant-sample): center_uv is loop-invariant.
        acc  += NoiseTex.SampleLevel(Linear, center_uv, 0) * (w * 0.01);
        wsum += w;
    }
    return acc / wsum;
}
```

### Good

```hlsl
// Hoist the loop-invariant sample before the loop.
float4 ps_loop_invariant_hoisted(float2 uv : TEXCOORD0) : SV_Target {
    // One TMU request instead of 16.
    float4 center_sample = Tex.Sample(Bilinear, Center);
    // The accumulated result is center_sample * (16 * 1/16) == center_sample.
    return center_sample;
}

// For the blur case: hoist the invariant noise sample.
float4 blur_with_hoisted_center(float2 uv) {
    float2 center_uv = uv;
    // Hoist before the loop — one TMU request for the invariant component.
    float4 center_noise = NoiseTex.SampleLevel(Linear, center_uv, 0);
    float4 acc  = 0;
    float  wsum = 0;
    for (uint i = 0; i < TapCount; ++i) {
        float  w = exp(-(float)i * (float)i / (2.0 * Sigma * Sigma));
        float2 offset = Direction * (float)i * InvRes;
        acc  += BlurTex.SampleLevel(Linear, uv + offset, 0) * w;
        acc  += center_noise * (w * 0.01);   // VGPR read — no TMU
        wsum += w;
    }
    return acc / wsum;
}
```

## Options

none

## Fix availability

**suggestion** — The suggested fix extracts the sample call to a new temporary before the loop. The suggestion is shown rather than machine-applied because the hoist changes the sampling point in the shader timeline, which matters for tiled-resource residency and sampler-feedback semantics. In practice these considerations are rare, but cannot be ruled out without understanding the resource bindings. The tool shows the suggested hoisted form with the new temporary name for human review.

## See also

- Related rule: [cbuffer-load-in-loop](cbuffer-load-in-loop.md) — loop-invariant cbuffer field reloaded each iteration (scalar; no TMU)
- Related rule: [small-loop-no-unroll](small-loop-no-unroll.md) — constant-bounded loop without [unroll]
- HLSL intrinsic reference: `Texture2D.Sample`, `Texture2D.SampleLevel`, `Texture2D.SampleGrad` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/loop-invariant-sample.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
