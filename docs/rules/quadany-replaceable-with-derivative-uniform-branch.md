---
id: quadany-replaceable-with-derivative-uniform-branch
category: wave-helper-lane
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# quadany-replaceable-with-derivative-uniform-branch

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `QuadAny(cond)` or `QuadAll(cond)` guard wrapping an `if`-branch whose body is derivative-uniform — every operation inside the branch either uses no derivatives or operates on values that are constant across the quad. In that case, the surrounding `if (QuadAny(cond))` adds no quad-correctness benefit and the simpler `if (cond)` is sufficient. The Phase 4 branch-shape detection identifies the wrapper pattern; the data-flow analysis verifies that the body has no derivative-bearing operations on quad-divergent values.

## Why it matters on a GPU

`QuadAny` (SM 6.7) is the canonical guard for keeping helper-lane participation alive across a per-lane branch in PS: when any lane in the quad wants to enter the branch, all four enter, and the helper lanes provide the derivative neighbours that texture sampling and `ddx`/`ddy` require. The cost is the wave-shuffle that `QuadAny` issues — typically 2-4 instructions on NVIDIA Turing/Ada Lovelace, AMD RDNA 2/3, and Intel Xe-HPG.

When the branch body has *no* derivative dependence on quad-divergent values — for instance, the body samples a texture using a quad-uniform UV — the helpers don't need to enter; the active lanes can compute their derivative neighbours themselves because all four lanes carry the same UV. The `QuadAny` wrapper becomes redundant: it pays the wave-shuffle cost without providing the helper participation it was added for.

The companion rule `quadany-quadall-opportunity` (locked in ADR 0011) detects the *opposite* direction — bare `if` that should be wrapped. This rule detects the case where an existing wrapper is redundant.

The fix is to drop the `QuadAny` and use the bare condition. The rule is suggestion-tier because the derivative-uniformity proof is approximate; the diagnostic emits the candidate rewrite as a comment.

## Examples

### Bad

```hlsl
// QuadAny wraps a branch whose body samples a quad-uniform texture; helpers
// don't need to participate.
Texture2D<float4>  g_Atlas   : register(t0);
SamplerState       g_Sampler : register(s0);
cbuffer Cfg : register(b0) { float2 g_AtlasUV; }

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0,0,0,0);
    if (QuadAny(uv.y > 0.5)) {        // wrapper redundant
        c = g_Atlas.Sample(g_Sampler, g_AtlasUV); // body uses uniform UV
    }
    return c;
}
```

### Good

```hlsl
// Drop the QuadAny — the bare condition suffices.
Texture2D<float4>  g_Atlas   : register(t0);
SamplerState       g_Sampler : register(s0);
cbuffer Cfg : register(b0) { float2 g_AtlasUV; }

float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = float4(0,0,0,0);
    if (uv.y > 0.5) {
        c = g_Atlas.Sample(g_Sampler, g_AtlasUV);
    }
    return c;
}
```

## Options

none

## Fix availability

**suggestion** — Removing the `QuadAny` is a textual deletion but requires confirming that the branch body has no quad-divergent derivative dependence. The diagnostic emits the candidate as a comment.

## See also

- Related rule: [quadany-quadall-opportunity](quadany-quadall-opportunity.md) — opposite-direction sibling rule
- Related rule: [quadany-quadall-non-quad-stage](quadany-quadall-non-quad-stage.md) — companion stage-validation rule
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — companion derivative-correctness rule
- HLSL specification: [SM 6.7 quad intrinsics](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_7.html)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/quadany-replaceable-with-derivative-uniform-branch.md)

*© 2026 NelCit, CC-BY-4.0.*
