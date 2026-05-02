---
id: bgra-rgba-swizzle-mismatch
category: texture
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# bgra-rgba-swizzle-mismatch

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A shader that reads `.rgba` (or any subset like `.r`, `.rgb`, `.gb`) from a `Texture2D<float4>` (or analogous typed view) whose underlying resource format, as reported by Slang reflection, is `DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB`, or `DXGI_FORMAT_B8G8R8X8_UNORM`, without a corresponding `.bgra` swizzle to compensate for the BGRA-to-RGBA channel order. The detector cross-references the sample call site's swizzle (or absence thereof) with the resource format from reflection. It does not fire on RGBA-format resources, nor on BGRA resources where the shader explicitly swizzles `.bgra` (or applies an equivalent component reorder).

## Why it matters on a GPU

`DXGI_FORMAT_B8G8R8A8_UNORM` is the historical swap-chain format for D3D presentation; many older paths (especially UI / IMGUI overlays composited onto the swap-chain) still use BGRA8 throughout. The hardware texture sampler on AMD RDNA 2/3, NVIDIA Turing/Ada, and Intel Xe-HPG reads BGRA8 storage and presents it to the shader as a `float4` whose `.x` lane carries the *blue* channel and `.z` lane carries the *red* channel — the storage order, not the conceptual RGBA order. There is no hardware swizzle on the load path on D3D12; the format-to-shader-view mapping is exactly the storage layout. (Vulkan exposes a per-view `componentMapping` swizzle that can compensate at descriptor creation; D3D12 does not.)

The bug surfaces when the shader treats the loaded `float4` as if `.r` is conceptually "red", uses it in subsequent colour math, and writes it to an RGBA8 render target. Red and blue channels swap silently. Visually the regression is striking — sky becomes orange, skin becomes blue — but the cause is buried in the BGRA-vs-RGBA convention difference between the swap-chain texture and the asset textures. UI overlays that mix swap-chain BGRA with asset RGBA are the canonical source of this bug.

The rule pulls the format/access mismatch up to lint time. The fix is one of: swizzle `.bgra` (or `.bgr`, etc.) at the sample site to convert BGRA storage to conceptual RGBA, or change the resource format to RGBA8 if the BGRA storage is incidental rather than required. On Vulkan/Metal targets the equivalent is to set the descriptor's component swizzle once at view creation, but the shader-side bug surfaces identically when the descriptor swizzle is left at identity.

## Examples

### Bad

```hlsl
Texture2D<float4> SwapChainCopy : register(t0);  // DXGI_FORMAT_B8G8R8A8_UNORM
SamplerState      LinearSampler : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    // .rgba on a BGRA8 storage delivers (B, G, R, A) — colours look swapped.
    return SwapChainCopy.Sample(LinearSampler, uv).rgba;
}
```

### Good

```hlsl
Texture2D<float4> SwapChainCopy : register(t0);  // DXGI_FORMAT_B8G8R8A8_UNORM
SamplerState      LinearSampler : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    // Swizzle to compensate for the BGRA storage order.
    return SwapChainCopy.Sample(LinearSampler, uv).bgra;
}
```

## Options

none

## Fix availability

**suggestion** — The fix is mechanical (`.rgba` -> `.bgra` at every load site against this resource) but its correctness depends on the application's intent for the BGRA format binding. The diagnostic identifies the mismatch and the candidate swizzle.

## See also

- Related rule: [manual-srgb-conversion](manual-srgb-conversion.md) — companion format-vs-shader-math mismatch
- DXGI reference: `DXGI_FORMAT_B8G8R8A8_UNORM` and the swap-chain format compatibility table
- HLSL reference: vector swizzles in the DirectX HLSL Language Syntax documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/bgra-rgba-swizzle-mismatch.md)

*© 2026 NelCit, CC-BY-4.0.*
