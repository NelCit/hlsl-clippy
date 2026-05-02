---
id: early-z-disabled-by-conditional-discard
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# early-z-disabled-by-conditional-discard

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Pixel shaders that contain `discard` (or its alias `clip(...)` for negative arguments) reachable from a non-uniform branch, when the entry point is not annotated with `[earlydepthstencil]`. The rule also flags shaders that write `SV_Depth` from any non-uniform control-flow path, since this likewise inhibits early-Z. The pattern is the silent, default-on case where a single late `discard` causes the driver to demote the entire shader to late-Z and pay the full cost of shading every covered fragment regardless of its visibility.

## Why it matters on a GPU

Modern GPUs perform depth/stencil testing at two possible points in the pipeline: before pixel shading (early-Z) or after pixel shading (late-Z). Early-Z is a major performance win: the depth comparison happens in fixed-function hardware that can reject hundreds of fragments per clock, before any shader work, before texture fetches, before any VGPR is allocated. Late-Z runs after the pixel shader has fully executed and can only update the depth buffer at the very end, after committing colour. The cost difference for a heavily-overdrawn scene can be 5-20x: an early-Z pass on a typical opaque deferred prepass at 1080p discards 80-95% of fragments before they cost anything; the same shader running with late-Z pays the full ALU and bandwidth cost for every fragment.

The driver decides between early-Z and late-Z by inspecting the shader. If the shader can `discard`, write `SV_Depth`, or call `InterlockedX` from a path that may execute, the depth value the rasterizer interpolated may not be the value that ends up in the depth buffer — and conservatively running the test late is the only correctness-safe choice. On AMD RDNA 2/3, this is the `DB_SHADER_CONTROL.Z_ORDER` pipeline state; the compiler emits hints to the driver based on whether a `discard` is found in the DXIL. On NVIDIA Turing and Ada, the equivalent decision is made in the geometry-shader/PS handoff layer (PPR / Z-cull state). On Intel Xe-HPG, the early-Z bypass is controlled by the `3DSTATE_PS_EXTRA` packet; the driver inspects DXIL for `discard` to set it.

The `[earlydepthstencil]` attribute on the pixel shader entry point forces early-Z on, opting the shader author into the contract that the depth value is fixed at rasterizer output regardless of any `discard`. This is correct when the `discard` only affects colour (alpha-test, masked materials) and not depth. Without the attribute, even a single guarded `discard` inside a rare branch causes late-Z for every fragment of every draw using the shader. The fix is either to annotate `[earlydepthstencil]` (when safe), to lift the alpha test into a separate masked-only depth prepass, or to restructure the shader to make the `discard` unconditional at entry so the compiler can prove it does not depend on shading work.

## Examples

### Bad

```hlsl
Texture2D    AlphaMask : register(t0);
SamplerState Samp      : register(s0);

float4 ps_alpha_test(float2 uv : TEXCOORD0) : SV_Target {
    float a = AlphaMask.Sample(Samp, uv).a;
    if (a < 0.5) {
        // Conditional discard with no [earlydepthstencil] annotation.
        // Driver demotes the shader to late-Z; every covered fragment pays
        // the full sampling cost even when the prepass already culled it.
        discard;
    }
    return float4(1, 1, 1, 1);
}
```

### Good

```hlsl
// Annotate [earlydepthstencil] when the discard does not depend on a
// computed depth value — the rasterizer-interpolated depth is correct
// even if the shader later discards colour.
[earlydepthstencil]
float4 ps_alpha_test_fixed(float2 uv : TEXCOORD0) : SV_Target {
    float a = AlphaMask.Sample(Samp, uv).a;
    if (a < 0.5) {
        discard;
    }
    return float4(1, 1, 1, 1);
}
```

## Options

none

## Fix availability

**suggestion** — Adding `[earlydepthstencil]` is safe only when the `discard` does not influence depth (no `SV_Depth` write tied to the same condition, no `discard` based on a modified depth value). The diagnostic flags the call and recommends the annotation, but a human must confirm the depth contract.

## See also

- Related rule: [discard-after-heavy-work](discard-after-heavy-work.md) — `discard` placement that wastes already-computed work
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — wave operations after `discard`
- HLSL attribute reference: `[earlydepthstencil]` in the DirectX HLSL specification
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/early-z-disabled-by-conditional-discard.md)

*© 2026 NelCit, CC-BY-4.0.*
