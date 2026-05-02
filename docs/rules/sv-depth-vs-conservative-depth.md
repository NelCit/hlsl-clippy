---
id: sv-depth-vs-conservative-depth
category: vrs
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# sv-depth-vs-conservative-depth

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

A pixel-shader entry that writes `SV_Depth` (a per-pixel depth override) without using one of the conservative-depth variants `SV_DepthGreaterEqual` or `SV_DepthLessEqual` when the value being written is monotonically related to the rasterised depth. The rule uses Slang reflection to identify the depth-output semantic on the PS entry, then walks the assignment expression: writes of the form `depth + bias`, `depth - bias`, `max(depth, k)`, or `min(depth, k)` are obvious conservative-direction patterns. Plain `SV_Depth` defeats early depth-stencil rejection on every IHV; the conservative variants preserve it under the matching ordering.

## Why it matters on a GPU

Early depth-stencil (early-Z, early-S) is a hidden-surface optimisation that runs the depth/stencil test *before* the pixel shader executes. On NVIDIA Turing/Ada Lovelace and AMD RDNA 2/3, the rasterizer hands eligible fragments to the PixelHash unit, and the GPU uses the rasterised depth to cull occluded pixels before allocating a wave. Intel Xe-HPG (Arc Alchemist, Battlemage) implements the same early-Z stage. When `SV_Depth` is written from PS without any ordering hint, the hardware cannot use the rasterised depth for the test — the actual depth might be anywhere — so it disables early-Z and runs the full PS to compute the depth, *then* tests. For a draw that would have been 50% occluded, this doubles the PS workload.

The SM 6.0 conservative-depth semantics (`SV_DepthGreaterEqual` / `SV_DepthLessEqual`) tell the hardware: "the value I write is bounded by the rasterised depth in this direction". When the depth-test direction matches (e.g., `SV_DepthGreaterEqual` with a `LESS` depth test), the hardware can perform the early test against the rasterised depth — anything that would fail the conservative bound also fails the actual write — and reject occluded pixels without ever invoking PS. The hardware path is the same one used for `[earlydepthstencil]`, just with a per-pixel override that stays compatible with the early-test invariant. AMD's compiler documentation and NVIDIA's Turing PS guide both call out this as a measurable win for forward+ depth-decal and depth-of-field passes — typical reported recoveries are 15-40% PS time on depth-bias-heavy decal passes.

When the bias is small and unidirectional, the conservative form is essentially free. When the bias is bidirectional (`depth + jitter()` where `jitter()` can be negative), neither conservative form applies and the rule does not fire — `SV_Depth` is genuinely required.

## Examples

### Bad

```hlsl
// Plain SV_Depth disables early-Z even though the bias is monotonic.
struct PSOut {
    float4 color : SV_Target0;
    float  depth : SV_Depth;
};

PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    PSOut o;
    o.color = SampleDecal(uv);
    o.depth = pos.z + 0.0001f; // depth bias toward the camera
    return o;
}
```

### Good

```hlsl
// Conservative-depth keeps early-Z. The depth test is LESS, so the
// PS-written value is always >= rasterised depth, satisfying the contract.
struct PSOut {
    float4 color : SV_Target0;
    float  depth : SV_DepthGreaterEqual;
};

PSOut main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    PSOut o;
    o.color = SampleDecal(uv);
    o.depth = pos.z + 0.0001f;
    return o;
}
```

## Options

none

## Fix availability

**suggestion** — The conservative-depth variant chosen depends on the depth test direction set in the PSO, which the linter cannot see from the shader alone. The diagnostic names the candidate (`SV_DepthGreaterEqual` for `+bias`, `SV_DepthLessEqual` for `-bias`) and asks the author to verify the test direction.

## See also

- Related rule: [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md) — `discard` defeats early-Z by the same mechanism
- Related rule: [vrs-incompatible-output](vrs-incompatible-output.md) — `SV_Depth` writes interact with VRS demotion
- D3D12 specification: SV_DepthGreaterEqual / SV_DepthLessEqual semantics, early depth-stencil rules
- Companion blog post: [vrs overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/sv-depth-vs-conservative-depth.md)

*© 2026 NelCit, CC-BY-4.0.*
