# hlsl-clippy Test Corpus

This directory contains a curated set of real, publicly licensed HLSL shaders used as the validation set for hlsl-clippy's linting rules. All shaders are sourced verbatim (or with only a mandatory header comment prepended) from open-source repositories under permissive licenses.

---

## Licensing Policy

**Only the following SPDX identifiers are permitted in this corpus:**

| SPDX ID | License Name |
|---|---|
| `MIT` | MIT License |
| `Apache-2.0` | Apache License 2.0 |
| `Apache-2.0 WITH LLVM-exception` | Apache 2.0 with LLVM runtime exception |
| `BSD-2-Clause` | BSD 2-Clause "Simplified" |
| `BSD-3-Clause` | BSD 3-Clause "New" or "Revised" |
| `CC0-1.0` | Creative Commons Zero v1.0 Universal |
| `CC-BY-4.0` | Creative Commons Attribution 4.0 |
| `Unlicense` | The Unlicense |

**Prohibited:** Any shader from a source that does not have an explicit, machine-readable open-source license in its repository root. "Source available" and "custom permissive" licenses require explicit approval before addition.

### How to vet a new corpus addition

1. Verify the **repository license** via `gh api repos/<owner>/<repo>/license` and check `spdx_id`. If it returns `NOASSERTION`, read the raw LICENSE file and confirm it is one of the approved identifiers above.
2. Check whether the **individual file** carries a different or additional license header. If so, that header governs; reject if it is not in the approved list.
3. Use a **pinned commit SHA** in the source URL (never a branch ref). Update the SHA when you intend to pick up a newer version.
4. Add the mandatory per-file header comment (see format below) as the very first lines of the file.
5. Update this README's inventory table.
6. If `slangc` is available, run `slangc -profile <doc-profile> <file>` and record pass/fail. Otherwise mark `verified: pending`.

### Mandatory per-file header format

```hlsl
// Source: <full URL to the file at a pinned commit>
// License: <SPDX identifier>
// Original author: <name or org>
// Profile: <slangc profile, e.g. ps_6_0, cs_6_5>
// Stage: <vertex|pixel|compute|raygen|miss|closesthit|mesh|amplification>
// Notes: <one-line summary of what the shader does>
// verified: <pass|fail(<error>)|pending>
```

---

## slangc Verification Status

`slangc` was **not found on PATH** at corpus-assembly time (2026-04-30). All files are marked `verified: pending`. Run the following command per file once slangc is installed:

```
slangc -profile <Profile> -entry main -target dxil <file.hlsl>
```

For library profiles (`lib_6_3`): use `-profile lib_6_3` without `-entry main`.

---

## Inventory

### vertex/ — Vertex Shaders (3 files)

| File | Source URL | License | Original Author | Profile | Stage | Description |
|---|---|---|---|---|---|---|
| `skinned_mesh_vs.hlsl` | [DefaultVS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/DefaultVS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | vs_5_0 | vertex | Skeletal-animation vertex shader with optional 4-bone skinning, tangent frame, and dual-UV support |
| `model_viewer_vs.hlsl` | [ModelViewerVS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/ModelViewerVS.hlsl) | MIT | Microsoft / Minigraph (James Stanard, Alex Nankervis) | vs_5_0 | vertex | Static-mesh vertex shader with world/shadow-space transforms, tangent frame, and optional triangle-ID encoding |
| `particle_instanced_vs.hlsl` | [ParticleVS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ParticleVS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | vs_5_0 | vertex | GPU-instanced billboard particle vertex shader; reads per-instance data from a StructuredBuffer using SV_InstanceID |

### pixel/ — Pixel/Fragment Shaders (3 files)

| File | Source URL | License | Original Author | Profile | Stage | Description |
|---|---|---|---|---|---|---|
| `model_viewer_ps.hlsl` | [ModelViewerPS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/ModelViewerPS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | ps_5_0 | pixel | Blinn-Phong PBR-style pixel shader with normal mapping, shadow sampling, SSAO, and MRT output |
| `skybox_ibl_ps.hlsl` | [SkyboxPS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/SkyboxPS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | ps_5_0 | pixel | IBL skybox pixel shader; samples a TextureCube at a specified mip level for HDR radiance display |
| `meshlet_blinn_phong_ps.hlsl` | [MeshletPS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12MeshShaders/src/MeshletRender/MeshletPS.hlsl) | MIT | Microsoft | ps_6_5 | pixel | Blinn-Phong pixel shader paired with the meshlet mesh shader; visualizes per-meshlet color or uniform diffuse |

### compute/ — Compute Shaders (7 files)

| File | Source URL | License | Original Author | Profile | Stage | Description |
|---|---|---|---|---|---|---|
| `gaussian_blur_cs.hlsl` | [BlurCS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/BlurCS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | cs_5_0 | compute | Separable 9-tap Gaussian blur using LDS ping-pong; packs two fp16 pixels per uint in groupshared memory |
| `tone_map_cs.hlsl` | [ToneMapCS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ToneMapCS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | cs_5_0 | compute | HDR tonemapping compute shader; applies Stanard operator, adds bloom, outputs log-luminance for auto-exposure |
| `particle_update_cs.hlsl` | [ParticleUpdateCS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ParticleUpdateCS.hlsl) | MIT | Microsoft / Minigraph (James Stanard, Julia Careaga) | cs_5_0 | compute | Particle simulation; integrates velocity/gravity, handles ground-plane rebound, writes billboard vertices |
| `generate_histogram_cs.hlsl` | [GenerateHistogramCS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/GenerateHistogramCS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | cs_5_0 | compute | Parallel-reduction luminance histogram; 16x16 group iterates a column using InterlockedAdd into groupshared then ByteAddressBuffer |
| `bloom_downsample_cs.hlsl` | [DownsampleBloomCS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/DownsampleBloomCS.hlsl) | MIT | Microsoft / Minigraph (James Stanard) | cs_5_0 | compute | Post-process bloom downsample; writes 4x and 16x reduced mips in one 8x8 group pass using LDS reduction |
| `fill_light_grid_cs.hlsl` | [FillLightGridCS.hlsli @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/FillLightGridCS.hlsli) | MIT | Microsoft / Minigraph (Alex Nankervis) | cs_5_0 | compute | Tiled deferred lighting; builds per-tile light lists by frustum-culling sphere/cone lights against min/max depth |
| `spd_cs_downsampler.hlsl` | [CSDownsampler.hlsl @ 7c796c6](https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/7c796c6d9fa6a9439e3610478148cfd742d97daf/sample/src/DX12/CSDownsampler.hlsl) | MIT | Advanced Micro Devices (AMD) | cs_6_0 | compute | AMD FidelityFX SPD downsampler with sRGB gamma conversion; writes to RWTexture2DArray using 8x8 thread groups |

### raytracing/ — Ray-Tracing Shaders (3 files)

| File | Source URL | License | Original Author | Profile | Stage | Description |
|---|---|---|---|---|---|---|
| `ray_generation.hlsl` | [RayGenerationShaderLib.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/RayGenerationShaderLib.hlsl) | MIT | Microsoft | lib_6_3 | raygen | Primary ray generation shader; generates camera rays per pixel and calls TraceRay, writing to a screen UAV |
| `miss_shader.hlsl` | [missShaderLib.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/missShaderLib.hlsl) | MIT | Microsoft | lib_6_3 | miss | Miss shader for primary and shadow rays; writes black background for non-reflection rays |
| `closest_hit_shader.hlsl` | [DiffuseHitShaderLib.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/DiffuseHitShaderLib.hlsl) | MIT | Microsoft / Minigraph (James Stanard, Christopher Wallis) | lib_6_3 | closesthit | Closest-hit shader with barycentric UV interpolation, normal mapping, PCF shadow rays, and Blinn-Phong shading |

### mesh/ — Mesh Shaders (1 file)

| File | Source URL | License | Original Author | Profile | Stage | Description |
|---|---|---|---|---|---|---|
| `meshlet_render_ms.hlsl` | [MeshletMS.hlsl @ b550a78](https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12MeshShaders/src/MeshletRender/MeshletMS.hlsl) | MIT | Microsoft | ms_6_5 | mesh | Mesh shader rendering meshlets; decodes packed 10-bit triangle indices and outputs vertex position/normal/meshlet-color |

---

## Summary

| Stage | Count | License(s) |
|---|---|---|
| vertex | 3 | MIT |
| pixel | 3 | MIT |
| compute | 7 | MIT (6), MIT/AMD (1) |
| raytracing | 3 | MIT |
| mesh | 1 | MIT |
| **Total** | **17** | |

**License breakdown:** 16 × MIT (microsoft/DirectX-Graphics-Samples), 1 × MIT (GPUOpen-Effects/FidelityFX-SPD / AMD)

---

## Sources Considered But Not Used

| Repo | Reason |
|---|---|
| `shader-slang/slang` tests/ | License is `Apache-2.0 WITH LLVM-exception` (permitted), but test shaders are micro-snippets targeting compiler edge cases — low value as validation corpus. May be added in a future pass. |
| `bkaradzic/bgfx` | License is BSD-2-Clause (permitted), but bgfx shaders use `.sc` (ShaderC) format with proprietary macro extensions — not native HLSL. Cannot be used without bgfx's shaderc pre-processor. |
| `google/filament` | Filament does not ship pre-compiled HLSL; it transpiles from its own material format. No stable HLSL source files found in the repository tree. |
| `GPUOpen-LibrariesAndSDKs/FidelityFX-SDK` | GitHub API returned `unknown` for license. The repo uses a custom AMD Open Source Software License 1.0, which is NOT on the approved list. Excluded. |
