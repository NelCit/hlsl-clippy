---
id: forcecase-missing-on-ps-switch
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# forcecase-missing-on-ps-switch

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `switch` statement inside a pixel-shader entry point where at least one case body contains a derivative-bearing operation — `Sample`, `SampleBias`, `SampleGrad`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, or any function call that transitively invokes one — and the `switch` lacks the `[forcecase]` attribute. The rule fires only on PS entry points (the hazard is specific to the quad / derivative model); compute and other stages are not flagged.

## Why it matters on a GPU

The HLSL `switch` statement has multiple valid lowerings: a true jump-table (one indirect branch, one taken target per lane), a chained `if`/`else` ladder (linear scan over case labels), or — in some compiler versions — a branch-free predicate fan that evaluates every case body and selects the right result. The `[forcecase]` attribute pins the compiler to the jump-table form. Without `[forcecase]`, the compiler is free to pick the chained-`if` form, and that's where the pixel-shader hazard appears: chained `if`s mean each case introduces a new branch, and per-pixel divergence on case selection retires quad lanes one-arm-at-a-time. Derivatives sampled inside the case body then see undefined neighbour values from retired lanes — exactly the same failure mode as a per-lane `if` containing a `Sample` (see [derivative-in-divergent-cf](derivative-in-divergent-cf.md) and [quadany-quadall-opportunity](quadany-quadall-opportunity.md)).

`[forcecase]` keeps the cases as a single jump-table dispatch, so the quad either takes one case (when uniform within the quad) or branches once with all four lanes still active. On AMD RDNA 2/3 the jump-table form preserves quad coherence at the SX (sequencer) level; NVIDIA Ada keeps the warp's quad participation intact through the indirect-branch instruction; Intel Xe-HPG's compiler honours `[forcecase]` similarly. The visible artefact when the hint is missing is mip-aliasing or seams at the boundary between case arms — a function-of-material-id `switch` that samples per-material textures is the canonical bad case.

The fix is one token: `[forcecase] switch (matId) { ... }`. The cost is essentially zero on modern compilers (the jump-table form is rarely worse than the chained-if form even when both would be correct) and the safety win is significant: derivative correctness becomes a property of the source code rather than a property of which compiler version was used. The rule fires on the missing-attribute case rather than dictating that every PS `switch` use the attribute; some `switch` statements have no derivative-bearing case bodies and the attribute is unnecessary there.

## Examples

### Bad

```hlsl
Texture2D<float4> g_AlbedoA : register(t0);
Texture2D<float4> g_AlbedoB : register(t1);
SamplerState g_Samp : register(s0);

float4 ps_per_material(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target {
    // No [forcecase] — compiler may emit chained-if; per-lane case selection
    // retires quad lanes individually and the Sample inside each arm sees
    // undefined derivative inputs from retired lanes.
    switch (matId) {
        case 0: return g_AlbedoA.Sample(g_Samp, uv);
        case 1: return g_AlbedoB.Sample(g_Samp, uv);
        default: return float4(0, 0, 0, 1);
    }
}
```

### Good

```hlsl
Texture2D<float4> g_AlbedoA : register(t0);
Texture2D<float4> g_AlbedoB : register(t1);
SamplerState g_Samp : register(s0);

float4 ps_per_material_safe(float2 uv : TEXCOORD0, uint matId : MAT_ID) : SV_Target {
    // [forcecase] pins the lowering to a jump-table dispatch; quad lanes
    // remain active through the indirect branch and derivatives stay valid.
    [forcecase]
    switch (matId) {
        case 0: return g_AlbedoA.Sample(g_Samp, uv);
        case 1: return g_AlbedoB.Sample(g_Samp, uv);
        default: return float4(0, 0, 0, 1);
    }
}
```

## Options

none

## Fix availability

**suggestion** — Adding `[forcecase]` is a one-token change but the developer may have a reason for the chained-if lowering (e.g., compiler-specific code-size considerations on a hot small-case switch). The diagnostic identifies the `switch` and the derivative-bearing case body so the author can apply the attribute deliberately.

## See also

- Related rule: [flatten-on-uniform-branch](flatten-on-uniform-branch.md) — sibling branch-attribute rule for `if`
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — underlying hazard
- Related rule: [quadany-quadall-opportunity](quadany-quadall-opportunity.md) — `if`-statement analogue
- HLSL reference: `[forcecase]` / `[call]` attributes on `switch` in the DirectX HLSL specification
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/forcecase-missing-on-ps-switch.md)

*© 2026 NelCit, CC-BY-4.0.*
