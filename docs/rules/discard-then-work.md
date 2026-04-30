---
id: discard-then-work
category: control-flow
severity: warn
applicability: none
since-version: v0.4.0
phase: 4
---

# discard-then-work

> **Status:** pre-v0 — rule scheduled for Phase 4; see [ROADMAP](../../ROADMAP.md).

## What it detects

Significant computation — texture samples, loops with multiple instructions, arithmetic chains longer than a configured threshold — that appears on a code path reachable only after a `discard` statement (or `clip(v)` with a potentially-negative argument) whose guard condition is non-uniform (per-pixel varying). The rule fires when the `discard` is inside an `if` whose predicate includes interpolated vertex attributes, texture reads, or other per-pixel varying data, and when the code following the `discard`-containing block is non-trivial. It does not fire when the `discard` is unreachable at runtime (e.g., guarded by a constant condition), when the subsequent work is a single arithmetic expression, or when `[earlydepthstencil]` is present (which changes the discard semantics in a way that makes the subsequent code less hazardous for helpers).

## Why it matters on a GPU

Pixel shaders execute in 2x2 pixel quads. When one or more pixels in a quad call `discard`, those pixels become helper lanes: they continue executing the shader for the remainder of the shader's work, but their results are never written to the framebuffer. Helper lanes exist because the quad's remaining active pixels still need screen-space derivative information (for `ddx`/`ddy` and for the implicit mip-level selection in `Texture.Sample`). Removing a helper lane prematurely would corrupt the derivatives of the surviving pixels.

This means that any work placed after a `discard` still executes on the discarded pixels at full shader cost. Texture samples, compute-heavy loops, and multi-tap blurs all run on helper lanes. The discarded pixel's ALU output is suppressed at the write-back stage, but the TMU requests, L1 texture cache pressure, register file consumption, and wave occupancy impact are paid in full. On a heavily alpha-tested scene — dense foliage, wire mesh, particle systems — the fraction of pixels that discard can exceed 50% of all pixels shaded. In that regime, post-discard work effectively doubles the per-pixel cost for a large proportion of screen coverage.

The practical fix is to move as much work as possible before the `discard`. Any value that does not depend on the result of the discarded path can be computed once before the discard guard and used on both the surviving and helper paths — without duplication. Where the post-discard work genuinely requires the surviving path's context (e.g., it depends on a value computed in the discard-free branch), the options are: restructure so the heavy sample is hoisted, use explicit-gradient sampling (`SampleGrad` / `SampleLevel`) which does not require the quad to be active, or accept the cost and suppress the diagnostic with an inline allow if profiling shows the path is cold. The rule is severity `warn` rather than `error` because the code is not undefined behaviour — it is legal and produces correct results — but carries a predictable performance regression that tools can surface.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 44-53
// HIT(discard-then-work): heavy work after discard runs on helper lanes
// for derivative computation; either reorder or annotate.
float4 ps_discard_then_work(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    if (uv.x < 0.0) discard;
    // This loop runs on all pixels including helper lanes — TMU cost paid twice.
    float4 result = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        result += Tex.Sample(Bilinear, uv + (float)i * 0.001);
    }
    return result / 16.0;
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 72-80
// HIT(discard-then-work): Sample after discard runs on helper lanes; heavy
// texture work here stresses derivative computation on masked pixels.
float4 ps_alpha_test_work(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 tex = ColorTex.Sample(BilinSS, uv);
    if (tex.a < 0.1) discard;
    float4 detail  = NoiseTex.Sample(BilinSS, uv * 8.0);
    float4 overlay = ColorTex.Sample(BilinSS, uv * 2.0 + Time * 0.01);
    return tex * detail + overlay * (1.0 - tex.a);
}
```

### Good

```hlsl
// Hoist heavy sampling before the discard guard.
float4 ps_discard_work_hoisted(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // Compute result before discarding — all quad lanes participate; derivatives valid.
    float4 result = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        result += Tex.Sample(Bilinear, uv + (float)i * 0.001);
    }
    result /= 16.0;
    // Discard happens after the work — helper lanes are already done.
    if (uv.x < 0.0) discard;
    return result;
}

// Alternative: use SampleLevel to avoid implicit-gradient dependency on helper lanes.
float4 ps_discard_sample_level(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 tex = Tex.SampleLevel(Bilinear, uv, 0);   // LOD 0 — no gradient needed
    if (tex.a < 0.1) discard;
    float4 detail  = NoiseTex.SampleLevel(Bilinear, uv * 8.0, 0);
    float4 overlay = ColorTex.SampleLevel(Bilinear, uv * 2.0, 0);
    return tex * detail + overlay * (1.0 - tex.a);
}
```

## Options

none

## Fix availability

**none** — Hoisting work before a `discard` changes the order of side effects and may change which texture resources are accessed on discarded pixels (with implications for sampler feedback and tiled resource residency). Data-flow analysis is required to prove the hoist is safe, and some reorganisations require understanding the algorithm's semantic intent. The diagnostic identifies the `discard` and the subsequent heavy work, but the restructuring is applied manually.

## See also

- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — implicit-gradient samples in non-uniform CF
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — wave intrinsics in PS where helper lanes may participate
- HLSL intrinsic reference: `discard`, `clip` in the DirectX HLSL Intrinsics documentation
- DirectX Specification: helper-lane semantics in the SM 6.x pixel-shader execution model
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/discard-then-work.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
