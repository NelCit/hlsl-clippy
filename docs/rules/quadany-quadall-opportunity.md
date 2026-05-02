---
id: quadany-quadall-opportunity
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# quadany-quadall-opportunity

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A pixel-shader `if (cond)` whose condition is a per-lane (quad-divergent) boolean and whose body issues at least one derivative-bearing operation — `Sample`, `SampleBias`, `SampleGrad`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, or any function call that transitively invokes one. The opportunity is to wrap the condition in `QuadAny(cond)`, so that whenever any lane in the 2x2 quad takes the branch, all four lanes participate as helpers and the derivative ops have valid neighbour samples. Companion (not duplicate) of the locked ADR 0010 rule `quadany-replaceable-with-derivative-uniform-branch` — that rule detects the *opposite* direction (replace `QuadAny` with a derivative-uniform predicate); this rule detects the forward direction (wrap a plain `if` in `QuadAny`).

## Why it matters on a GPU

Pixel-shader derivatives (`ddx`, `ddy`, and the implicit derivatives consumed by `Sample`) are computed by differencing values across the 2x2 quad of neighbouring pixels. The hardware requires all four quad lanes to be active — even pixels outside the rendered triangle remain active as "helper lanes" specifically to supply derivative samples. When a per-lane `if` retires some quad lanes (because their condition is false), the derivative inputs from those lanes become undefined: AMD RDNA 2/3 returns implementation-defined values for the derivative tap from a retired lane, NVIDIA Ada produces undefined sampler LOD on the surviving lanes' implicit-derivative samples, and Intel Xe-HPG behaves similarly. The visible artefact is mip-aliasing or seams at the boundary between branch-taken and branch-not-taken regions.

`QuadAny(cond)` returns true on every lane in the 2x2 quad if *any* lane has `cond == true`. Wrapping the branch with `if (QuadAny(cond))` keeps all four quad lanes active inside the body, so the derivative ops see consistent neighbour samples. The cost is that lanes whose `cond` was false also execute the body — but they already had to remain active as helpers anyway, so the only added cost is the body's ALU work on the helper lanes (and their writes to render targets are gated by the original `cond` if needed). For samplers in particular, the helper-lane participation is essentially free because the sampler hardware schedules the four-tap quad as one transaction regardless.

The corresponding `QuadAll(cond)` pairs with the rare case where *all* quad lanes must agree before entering a branch — for example, a coarse-shading skip that should run only when all four pixels of the quad opt out. The rule fires only on the `QuadAny` direction by default; the `QuadAll` direction is rarer and produces too many false positives without further heuristics. Authors should be able to suppress one without the other if they cite a reason — the suppression key is the rule ID, not the intrinsic.

## Examples

### Bad

```hlsl
Texture2D<float4> g_Albedo : register(t0);
SamplerState g_Samp : register(s0);

float4 ps_masked(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target {
    if (matId == MAT_ALBEDO_SAMPLED) {
        // Per-lane branch; quad mates with matId != MAT_ALBEDO_SAMPLED retire,
        // so this Sample sees undefined derivative inputs from those lanes.
        // Mip-aliasing artefacts on the boundary.
        return g_Albedo.Sample(g_Samp, uv);
    }
    return float4(0, 0, 0, 1);
}
```

### Good

```hlsl
Texture2D<float4> g_Albedo : register(t0);
SamplerState g_Samp : register(s0);

float4 ps_masked_quad(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target {
    // Wrap the per-lane condition: when any quad lane needs the sample,
    // all four participate as helpers and derivatives are valid.
    if (QuadAny(matId == MAT_ALBEDO_SAMPLED)) {
        float4 albedo = g_Albedo.Sample(g_Samp, uv);
        // Gate the *write* on the original per-lane condition; helper lanes
        // sampled but their result is discarded.
        return (matId == MAT_ALBEDO_SAMPLED) ? albedo : float4(0, 0, 0, 1);
    }
    return float4(0, 0, 0, 1);
}
```

## Options

none

## Fix availability

**suggestion** — The transformation requires understanding which lanes need the helper-lane participation and which lanes' results should be written. The diagnostic identifies the per-lane condition and the derivative-bearing op so the author can apply the wrap correctly.

## See also

- Related rule: `quadany-replaceable-with-derivative-uniform-branch` (ADR 0010, queued) — opposite-direction sibling
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — the underlying hazard
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — helper-lane participation issues
- HLSL intrinsic reference: `QuadAny`, `QuadAll`, `QuadReadLaneAt` (SM 6.7+)
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/quadany-quadall-opportunity.md)

*© 2026 NelCit, CC-BY-4.0.*
