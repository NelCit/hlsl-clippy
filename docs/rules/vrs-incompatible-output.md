---
id: vrs-incompatible-output
category: vrs
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# vrs-incompatible-output

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

Pixel-shader entry points that write per-sample outputs (`SV_Coverage`, `SV_SampleIndex`, an `[earlydepthstencil]`-marked `SV_Depth`, or per-sample interpolated inputs marked `sample`) while a Variable Rate Shading (VRS) shading rate coarser than 1x1 is applied to the draw. The rule uses Slang reflection to enumerate the entry's output semantics and pixel-shader attributes, then flags combinations that the D3D12 VRS specification calls out as forcing the runtime to silently drop the shading-rate request and revert to per-pixel shading. Common offenders: a shader that writes both a coarse colour and `SV_Coverage`, or a deferred g-buffer pass that uses VRS Tier 2 image-based shading rates while still emitting MSAA-aware per-sample inputs.

## Why it matters on a GPU

VRS is a coarse-shading optimisation: NVIDIA Turing and Ada Lovelace expose Tier 1 (per-draw) and Tier 2 (image-based + per-primitive) shading rates that let one PS invocation cover up to a 4x4 pixel region. AMD RDNA 2/3 implements the same surface as Variable Rate Shading at the rasterizer, with hardware that broadcasts the single shaded result across the coarse footprint. Intel Xe-HPG (Arc/Battlemage) added VRS Tier 2 with the same semantics. The whole point is to amortise PS work across multiple raster samples — the wave executes one set of derivatives, one set of sample fetches, and one ALU sequence per coarse fragment.

The VRS specification says clearly that any output that varies *per sample* — `SV_Coverage`, per-sample `SV_Depth`, `SV_SampleIndex` reads, `sample`-qualified inputs — forces the shading rate back to 1x1 because the hardware cannot legally produce one coarse value that satisfies a per-sample contract. On all three IHVs the runtime handles this by silently demoting the rate. The shader still pays for being PS-VRS-marked (some IHVs serialise the rasterizer differently for VRS-eligible draws even when the rate falls back), but the savings the author expected never materialise. On a g-buffer pass that nominally runs at 2x2 VRS, the demotion can quietly cost 30-60% of the projected PS budget.

The fix is a structural one: separate the per-sample work into its own draw or pass that does not use VRS, or remove the per-sample output if the per-pixel approximation is acceptable. The rule cannot rewrite this for the author; it surfaces the conflict so the author can decide whether the VRS request was wishful thinking.

## Examples

### Bad

```hlsl
// PS uses VRS image-rate coarse shading, but emits SV_Coverage —
// runtime silently demotes to 1x1.
struct PSOut {
    float4 color    : SV_Target0;
    uint   coverage : SV_Coverage;
};

[earlydepthstencil]
PSOut main(float4 pos : SV_Position, sample float2 uv : TEXCOORD0) {
    PSOut o;
    o.color    = g_Albedo.Sample(g_Sampler, uv);
    o.coverage = 0xFFu;
    return o;
}
```

### Good

```hlsl
// VRS-friendly PS: no per-sample inputs, no SV_Coverage. Coarse
// shading is honoured on Turing/Ada/RDNA 2-3/Xe-HPG.
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    return g_Albedo.Sample(g_Sampler, uv);
}
```

## Options

none

## Fix availability

**suggestion** — The fix requires either splitting the pass or removing a per-sample output, both of which change application-visible behaviour. The diagnostic names the offending semantic so the author can act, but the rewrite is manual.

## See also

- Related rule: [excess-interpolators](excess-interpolators.md) — interpolator pressure interacts with VRS savings
- Related rule: [sv-depth-vs-conservative-depth](sv-depth-vs-conservative-depth.md) — `SV_Depth` writes interact with VRS in the same way
- D3D12 specification: Variable Rate Shading and SV semantic compatibility table
- Companion blog post: [vrs overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/vrs-incompatible-output.md)

*© 2026 NelCit, CC-BY-4.0.*
