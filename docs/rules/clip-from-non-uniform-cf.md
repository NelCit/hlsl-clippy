---
id: clip-from-non-uniform-cf
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# clip-from-non-uniform-cf

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A call to `clip(x)` (the HLSL component-wise discard intrinsic that retires the lane when any component of `x` is negative) inside a pixel-shader entry point whose containing function lacks the `[earlydepthstencil]` attribute and is reachable from non-uniform control flow — that is, the path from entry to the `clip` call passes through at least one branch whose condition is per-lane varying. Distinct from the locked [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md), which fires on `discard`; `clip(x)` has its own semantics (retire on negative component, threshold per channel) and an independent suppression scope.

## Why it matters on a GPU

Pixel-shader early-Z (and stencil) testing is a critical bandwidth optimisation: the rasteriser tests depth before invoking the pixel shader, so occluded fragments never run. A pixel shader that may issue `clip(x)` (or `discard`) modifies the depth attachment as a side effect of the shader body, which forces the hardware to defer depth update until after shader execution. On AMD RDNA 2/3, the GE / SX hardware downgrades from "early-Z + early-stencil" to "late-Z" for the entire pipeline state when the shader contains a clip / discard reachable on any path — losing the bandwidth savings on every fragment, not just the clipped ones. NVIDIA Ada applies the same downgrade per-pipeline-state; Intel Xe-HPG behaves analogously. The shader author can opt back into early-Z by adding the `[earlydepthstencil]` attribute, which promises the hardware that the depth value is unaffected by the shader (the clip / discard then retires the lane *after* the early-Z test has already updated depth, accepting the resulting incorrect depth in exchange for the bandwidth recovery — a trade-off only the author can authorise).

The non-uniform-CF flavour is the worst case because the optimiser cannot hoist the `clip` to a uniform location: lanes diverge on the predicate and clip independently. Even on hardware that supports per-lane retirement without warp-level disruption, the resulting pixel-quad fragmentation costs derivative-uniformity — the surviving lanes in a quad may need helper-lane participation to maintain `ddx`/`ddy` accuracy, and the retired lanes are still invoked as helpers (consuming ALU). The bandwidth cost from losing early-Z is the dominant term; the helper-lane fragmentation is a second-order tax.

The fix is one of: (a) add `[earlydepthstencil]` if the algorithm tolerates the depth-test-before-clip ordering, (b) hoist the `clip` to a uniform predicate (typical: clip on a wave-uniform function-of-vertex-input), or (c) restructure to use alpha-test in the rasteriser pipeline state instead of an in-shader clip. Each has cost; the rule surfaces the hazard rather than mandating a resolution. The rule is intentionally distinct from [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md) because authors expect to suppress one without the other — `clip` and `discard` have different semantics in source even when they collapse to the same HW retirement.

## Examples

### Bad

```hlsl
Texture2D<float4> g_Mask : register(t0);
SamplerState g_Samp : register(s0);

float4 ps_clipped(float2 uv : TEXCOORD0) : SV_Target {
    float4 mask = g_Mask.Sample(g_Samp, uv);
    // Per-pixel divergent clip — disables early-Z for the whole pipeline state
    // because no [earlydepthstencil] attribute on the entry point.
    clip(mask.a - 0.5);
    return float4(shade(uv), 1.0);
}
```

### Good

```hlsl
// Option A — opt into early-Z, accepting that depth updates before the clip
// retires the lane. Valid only if the depth value the shader writes is
// independent of the clip outcome.
Texture2D<float4> g_Mask : register(t0);
SamplerState g_Samp : register(s0);

[earlydepthstencil]
float4 ps_clipped_earlyz(float2 uv : TEXCOORD0) : SV_Target {
    float4 mask = g_Mask.Sample(g_Samp, uv);
    clip(mask.a - 0.5);
    return float4(shade(uv), 1.0);
}

// Option B — move the clip to alpha-test in the PSO and drop the in-shader clip.
// (Pseudocode — done in the application's pipeline-state setup, not the shader.)
float4 ps_no_clip(float2 uv : TEXCOORD0) : SV_Target {
    return float4(shade(uv), g_Mask.Sample(g_Samp, uv).a);
}
```

## Options

none

## Fix availability

**suggestion** — Adding `[earlydepthstencil]` is a semantic change (depth update reordering); moving to alpha-test is an application-side change. The diagnostic identifies the `clip` call, the divergent predicate path, and the missing attribute so the author can decide.

## See also

- Related rule: [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md) — sibling rule for `discard`
- Related rule: [discard-then-work](discard-then-work.md) — wasted work after retirement
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — derivative hazard from quad fragmentation
- HLSL reference: `clip` intrinsic; `[earlydepthstencil]` attribute
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/clip-from-non-uniform-cf.md)

*© 2026 NelCit, CC-BY-4.0.*
