# Test corpus — sources, licenses, provenance

This file is the canonical per-shader registry for everything under `tests/corpus/`.
Adding a corpus shader requires adding a row here. Acceptable licenses are listed in
[docs/decisions/0006-license-apache-2-0.md](../../docs/decisions/0006-license-apache-2-0.md).

> **Note:** `verified:` columns in individual shader headers and the Verified column
> below track `slangc` compilation status. All files are `pending` until the Phase 1
> build exposes `slangc` on PATH in CI. See the Verification Status section below.

---

## Shader registry

| File | Stage | Profile | Source URL (pinned) | License | SPDX | Author / Org | Verified |
|------|-------|---------|---------------------|---------|------|--------------|----------|
| `vertex/skinned_mesh_vs.hlsl` | vertex | vs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/DefaultVS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `vertex/model_viewer_vs.hlsl` | vertex | vs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/ModelViewerVS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard, Alex Nankervis) | pending |
| `vertex/particle_instanced_vs.hlsl` | vertex | vs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ParticleVS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `vertex/depth_prepass_vs.hlsl` | vertex | vs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/84c39f61c5e1a1c820e0edfc3cd608a59bed4fa3/MiniEngine/Model/Shaders/DepthOnlyVS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `pixel/model_viewer_ps.hlsl` | pixel | ps_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/ModelViewerPS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `pixel/skybox_ibl_ps.hlsl` | pixel | ps_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/SkyboxPS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `pixel/meshlet_blinn_phong_ps.hlsl` | pixel | ps_6_5 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12MeshShaders/src/MeshletRender/MeshletPS.hlsl | MIT License | MIT | Microsoft | pending |
| `pixel/wave_intrinsics_ps.hlsl` | pixel | ps_6_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/ca3f7f4e0a73b69f96371fef0f4a08958b278936/Samples/Desktop/D3D12SM6WaveIntrinsics/src/wave.hlsl | MIT License | MIT | Microsoft | pending |
| `compute/gaussian_blur_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/BlurCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `compute/tone_map_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ToneMapCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `compute/particle_update_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/ParticleUpdateCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard, Julia Careaga) | pending |
| `compute/generate_histogram_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/GenerateHistogramCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `compute/bloom_downsample_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Core/Shaders/DownsampleBloomCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `compute/fill_light_grid_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/MiniEngine/Model/Shaders/FillLightGridCS.hlsli | MIT License | MIT | Microsoft / Minigraph (Alex Nankervis) | pending |
| `compute/spd_cs_downsampler.hlsl` | compute | cs_6_0 | https://github.com/GPUOpen-Effects/FidelityFX-SPD/blob/7c796c6d9fa6a9439e3610478148cfd742d97daf/sample/src/DX12/CSDownsampler.hlsl | MIT License | MIT | Advanced Micro Devices (AMD) | pending |
| `compute/nbody_gravity_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/d5eefe9ac24e67bd57170e288e4e9490d5fdd067/Samples/Desktop/D3D12nBodyGravity/src/nBodyGravityCS.hlsl | MIT License | MIT | Microsoft | pending |
| `compute/indirect_cull_cs.hlsl` | compute | cs_5_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/d5eefe9ac24e67bd57170e288e4e9490d5fdd067/Samples/Desktop/D3D12ExecuteIndirect/src/compute.hlsl | MIT License | MIT | Microsoft | pending |
| `compute/bitonic_presort_cs.hlsl` | compute | cs_6_0 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/84c39f61c5e1a1c820e0edfc3cd608a59bed4fa3/MiniEngine/Core/Shaders/Bitonic32PreSortCS.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard) | pending |
| `compute/hello_work_graph_wg.hlsl` | node | lib_6_8 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/51d0c1c5e225186a279bcdf15b7dbf68745301db/Samples/Desktop/D3D12HelloWorld/src/HelloWorkGraphs/D3D12HelloWorkGraphs.hlsl | MIT License | MIT | Microsoft | pending |
| `raytracing/ray_generation.hlsl` | raygen | lib_6_3 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/RayGenerationShaderLib.hlsl | MIT License | MIT | Microsoft | pending |
| `raytracing/miss_shader.hlsl` | miss | lib_6_3 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/missShaderLib.hlsl | MIT License | MIT | Microsoft | pending |
| `raytracing/closest_hit_shader.hlsl` | closesthit | lib_6_3 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/DiffuseHitShaderLib.hlsl | MIT License | MIT | Microsoft / Minigraph (James Stanard, Christopher Wallis) | pending |
| `raytracing/rt_simple_lighting.hlsl` | raygen | lib_6_3 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/104dd7b5b4b4d3e440349dce697a1b404dfa090f/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl | MIT License | MIT | Microsoft | pending |
| `raytracing/rt_procedural_geometry.hlsl` | raygen | lib_6_3 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/a6c390db40f5ddb5d2a2279d1adc64b3df53de9f/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingProceduralGeometry/Raytracing.hlsl | MIT License | MIT | Microsoft | pending |
| `mesh/meshlet_render_ms.hlsl` | mesh | ms_6_5 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/b550a78e815bbc8b7a0fd948efab11c01d9a1ed2/Samples/Desktop/D3D12MeshShaders/src/MeshletRender/MeshletMS.hlsl | MIT License | MIT | Microsoft | pending |
| `amplification/meshlet_cull_as.hlsl` | amplification | as_6_5 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/a365224720921b4e5cab8ddf68b76af36d035394/Samples/Desktop/D3D12MeshShaders/src/MeshletCull/MeshletAS.hlsl | MIT License | MIT | Microsoft | pending |
| `amplification/dynamic_lod_as.hlsl` | amplification | as_6_5 | https://github.com/microsoft/DirectX-Graphics-Samples/blob/cbd20a1910042b5c3dec62676abe4c36395be3d5/Samples/Desktop/D3D12MeshShaders/src/DynamicLOD/MeshletAS.hlsl | MIT License | MIT | Microsoft | pending |

---

## License breakdown

| SPDX | Count | Source repos |
|------|-------|--------------|
| MIT | 27 | microsoft/DirectX-Graphics-Samples (25), GPUOpen-Effects/FidelityFX-SPD (1), plus wave_intrinsics_ps from DirectX-Graphics-Samples (1) |

**Total: 27 shaders — 27 × MIT**

---

## Stage breakdown

| Stage | Count | Files |
|-------|-------|-------|
| vertex | 4 | skinned_mesh_vs, model_viewer_vs, particle_instanced_vs, depth_prepass_vs |
| pixel | 4 | model_viewer_ps, skybox_ibl_ps, meshlet_blinn_phong_ps, wave_intrinsics_ps |
| compute | 10 | gaussian_blur_cs, tone_map_cs, particle_update_cs, generate_histogram_cs, bloom_downsample_cs, fill_light_grid_cs, spd_cs_downsampler, nbody_gravity_cs, indirect_cull_cs, bitonic_presort_cs |
| node (work graph) | 1 | hello_work_graph_wg |
| raygen | 4 | ray_generation, rt_simple_lighting, rt_procedural_geometry (library with raygen entry) |
| miss | 1 | miss_shader |
| closesthit | 1 | closest_hit_shader |
| mesh | 1 | meshlet_render_ms |
| amplification | 2 | meshlet_cull_as, dynamic_lod_as |
| **Total** | **27** | |

> Note: `rt_simple_lighting.hlsl` and `rt_procedural_geometry.hlsl` contain multiple shader
> entry points (raygen + closesthit + miss / raygen + closesthit + miss + intersection) compiled
> as DXR library profiles. The Stage column records the primary entry point (raygen).

---

## Vetting checklist (for new additions)

1. Open the upstream URL at a specific commit SHA.
2. Locate the `LICENSE` / `LICENSE-MIT` / `LICENSE.md` / SPDX header in the source repo.
3. Confirm the SPDX matches one of: `MIT`, `Apache-2.0`, `Apache-2.0 WITH LLVM-exception`,
   `BSD-2-Clause`, `BSD-3-Clause`, `CC0-1.0`, `CC-BY-4.0`, `Unlicense`.
4. Add the row to the table above.
5. Add the file to `tests/corpus/<stage>/<name>.hlsl` with the standard header block.
6. Do **not** use `FidelityFX-SDK` proper (AMD OSS License 1.0 — non-permissive). FidelityFX
   effect sub-repos (e.g. `GPUOpen-Effects/FidelityFX-SPD`) ship their own MIT license and
   are permitted.

---

## Verification status

`slangc` verification (`slangc -profile <profile> file.hlsl`) is **pending** for the entire
corpus. `slangc` was not on PATH at corpus-assembly time (2026-04-30). Once Phase 1 lands
the build will expose `build/_deps/slang-build/.../slangc.exe`; a future agent should run:

```
slangc -profile <Profile> -entry main -target dxil <file.hlsl>
```

For library profiles (`lib_6_3`, `lib_6_8`): use `-profile lib_6_3` (no `-entry main`).

Files marked `verified: pending` in their headers **must** be flipped to `verified: ok` (or
`verified: fail(<error>)`) when the verification job lands.

### Known compilation caveats

- `amplification/meshlet_cull_as.hlsl` and `amplification/dynamic_lod_as.hlsl` include
  `MeshletCommon.hlsli` / `Shared.h` / `Common.hlsli` respectively. These headers must be
  present for compilation; they are intentionally absent here since this corpus tests the
  linter's AST/token parsing, not end-to-end compilation.
- `compute/bitonic_presort_cs.hlsl` includes `BitonicSortCommon.hlsli`. Same caveat.
- `vertex/depth_prepass_vs.hlsl` includes `Common.hlsli`. Same caveat.
- `compute/hello_work_graph_wg.hlsl` requires SM 6.8 support in the `slangc` build.
- `raytracing/rt_procedural_geometry.hlsl` includes `ProceduralPrimitivesLibrary.hlsli`
  and `RaytracingShaderHelper.hlsli`. Same caveat.

---

## Sources audited but not added

| Repo / file | Reason |
|-------------|--------|
| `shader-slang/slang tests/` | `Apache-2.0 WITH LLVM-exception` (permitted). Test shaders are micro-snippets targeting compiler edge cases — low linting value. May be added in a future pass. |
| `bkaradzic/bgfx` | BSD-2-Clause (permitted). Shaders use `.sc` (ShaderC) format with proprietary macro extensions — not native HLSL without bgfx shaderc pre-processor. |
| `google/filament` | Apache-2.0 (permitted in principle). Filament transpiles from its own material format; no stable standalone HLSL files in the repository tree. |
| `GPUOpen-LibrariesAndSDKs/FidelityFX-SDK` | AMD Open Source Software License 1.0 — **not** on the approved list. Excluded by ADR 0006. |
| `MiniEngine/Core/Shaders/AoRender1CS.hlsl` | MIT (permitted). File is a one-line stub that `#include`s `AoRenderCS.hlsli`; the actual logic lives in the hlsli. Would require bundling the include. Deferred. |
| `D3D12RaytracingRealTimeDenoisedAmbientOcclusion/RTAO/Shaders/RTAO.hlsl` | MIT (permitted). Deeply include-dependent (`RaytracingShaderHelper.hlsli`, `RandomNumberGenerator.hlsli`, `Ray Sorting/RaySorting.hlsli`, `RTAO.hlsli`). Deferred until include-bundling is supported. |
