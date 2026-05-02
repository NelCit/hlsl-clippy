---
id: mip-clamp-zero-on-mipped-texture
category: texture
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# mip-clamp-zero-on-mipped-texture

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `SamplerState` whose descriptor pins `MaxLOD = 0` (or a `Sample`/`SampleLevel` call with `clamp = 0` arg, or a `MinMipLevel = 0` clamp on a sampler-feedback path) bound against a `Texture2D` / `Texture2DArray` / `TextureCube` resource that reflection reports as carrying more than one mip level. The detector cross-references the sampler's `MaxLOD` field via reflection with the resource's mip count via reflection. It does not fire on textures that are genuinely single-mip (loading a single-level surface with `MaxLOD = 0` is correct and intended); it fires only when mips exist and the sampler clamps them off.

## Why it matters on a GPU

Mipmaps on every desktop GPU exist for two reasons: anti-aliasing minified samples (the higher-frequency texels of mip 0 alias under minification, so the hardware blends in coarser mips when the screen-space derivatives indicate minification) and bandwidth amortisation (a coarser mip is 1/4 the texel count and 1/4 the bandwidth). The texture sampler unit on AMD RDNA 2/3 (TMU), NVIDIA Turing/Ada (TEX/L1), and Intel Xe-HPG samples mips by computing a fractional LOD from the screen-space derivatives, then trilinearly blending two adjacent mips. When `MaxLOD = 0`, the hardware clamps the LOD selection to mip 0 regardless of derivatives — minified samples re-enter the aliasing regime and bandwidth scales with screen footprint instead of texel footprint.

The bandwidth cost is the easier to quantify. A terrain or streaming-virtual-texture pass that loads a 4096x4096 surface with `MaxLOD = 0` reads from the full mip 0 working set on every minified sample. With trilinear filtering across mip 0 and mip 1, the same minified pixel reads at 1/4 + 1/16 = ~31% of the mip-0 bandwidth and the L1 cache hit rate climbs because adjacent pixels touch overlapping mip-1 footprints. On RDNA 3 with its 16 KB L1 per CU, the difference shows up as a measurable cache hit rate change in any minified-texture-bound pass.

The aliasing cost is the silent one. `MaxLOD = 0` re-introduces shimmering and crawling on minified geometry that the asset's mip chain was authored to suppress. It is also a common copy-paste accident: a developer pins `MaxLOD = 0` to debug LOD selection, then forgets to reset it. The rule fires whenever reflection confirms the texture has mips and the sampler discards them.

## Examples

### Bad

```hlsl
// Sampler clamps MaxLOD to 0 — discards every mip the texture carries.
SamplerState ClampedSampler : register(s0);  // MaxLOD = 0 in descriptor
Texture2D<float4> Diffuse  : register(t0);   // 11 mips: 1024 -> 1

float4 sample_terrain(float2 uv) {
    return Diffuse.Sample(ClampedSampler, uv);  // always reads mip 0
}
```

### Good

```hlsl
// Default sampler — MaxLOD = D3D12_FLOAT32_MAX; trilinear blend across
// mips driven by ddx/ddy of uv.
SamplerState TrilinearSampler : register(s0);  // MaxLOD = FLT_MAX
Texture2D<float4> Diffuse     : register(t0);

float4 sample_terrain(float2 uv) {
    return Diffuse.Sample(TrilinearSampler, uv);
}
```

## Options

none

## Fix availability

**suggestion** — Removing the `MaxLOD = 0` clamp changes the visual output (mip filtering takes over). The diagnostic flags the mismatch; the author confirms the visual change is intended.

## See also

- Related rule: [anisotropy-without-anisotropic-filter](anisotropy-without-anisotropic-filter.md) — sampler state field that has no effect under the chosen filter
- Related rule: [comparison-sampler-without-comparison-op](comparison-sampler-without-comparison-op.md) — sampler descriptor type unused at the call site
- D3D12 reference: `D3D12_SAMPLER_DESC` field documentation, mip filtering in the D3D12 sampler documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mip-clamp-zero-on-mipped-texture.md)

*© 2026 NelCit, CC-BY-4.0.*
