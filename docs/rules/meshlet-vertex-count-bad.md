---
id: meshlet-vertex-count-bad
category: mesh
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# meshlet-vertex-count-bad

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Mesh-shader entry points whose `[outputtopology(...)]` + `out vertices N` + `out indices M` declarations choose meshlet sizes outside the per-vendor sweet-spot range. The rule fires when `N` (vertex count) or `M` (index count, expressed as triangles) lands in a known-pathological band — for example, 96 vertices on RDNA 2/3 (which prefers powers of two up to 64 or 128) or 128 vertices on NVIDIA Ada Lovelace (which prefers 64 for compute-bound passes and 128 only when index-bandwidth-bound). The check consults a small built-in table of vendor sweet spots derived from public driver guidance and AMD/NVIDIA mesh-shader best-practice documents.

## Why it matters on a GPU

Mesh shaders dispatch a wave per meshlet, and the wave's vertex output is held in on-chip groupshared memory until the rasteriser consumes it. AMD RDNA 2 and RDNA 3 use SIMD32 waves and want meshlet vertex counts that are exact multiples of the wave width: 32, 64, 96, or 128 vertices map to 1, 2, 3, or 4 vertex-shading iterations per wave. The 96-vertex case is the trap — it requires three iterations, none of which can fully utilise the 32-lane SIMD when triangle indices reference vertices unevenly. AMD's mesh-shader guidance recommends 64 vertices and 124 primitives as the default sweet spot for RDNA 2/3, with 128 vertices acceptable when the meshlet is geometry-dense.

NVIDIA Ada Lovelace (and Ampere before it) uses 32-thread warps and supports up to 256 vertices and 256 primitives per meshlet, but the optimal point shifts with workload: 64 vertices / 84 primitives for compute-heavy meshlets (skinning, tessellation) and 128 vertices / 128 primitives only when the bottleneck is the input-assembler-equivalent index bandwidth. NVIDIA's published Mesh Shader Best Practices show roughly 15-25% throughput differences between the optimal and a naive 256/256 choice on Asteroids-style benchmarks. Intel Xe-HPG (Arc Alchemist, Battlemage) sits between the two: 64/124 is a safe baseline, with the caveat that Xe-HPG's mesh-shader emulation path on driver versions prior to the late-2024 stack performs poorly at 256 vertices.

The takeaway is that there is no single portable "right" meshlet size. The rule does not pick one — it identifies the cases where a chosen value is suboptimal on every common architecture (96 vertices, 200 vertices, sub-32 vertex counts that waste a SIMD32 wave) and recommends one of the per-vendor sweet spots from the configuration. For shipping cross-vendor titles, the standard advice is to pre-compute meshlets at 64 vertices / 124 primitives offline, since it sits within the optimal band on both RDNA and Ada and is a safe upper bound for Intel Xe-HPG.

## Examples

### Bad

```hlsl
// 96 vertices: 3 SIMD32 iterations on RDNA, none fully packed; pessimal on AMD.
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    in uint gtid : SV_GroupThreadID,
    in uint gid  : SV_GroupID,
    out vertices  VertexOut verts[96],
    out indices   uint3      tris[128])
{
    SetMeshOutputCounts(96, 128);
    // ...
}
```

### Good

```hlsl
// 64 vertices / 124 primitives: in the sweet spot for RDNA 2/3, Ada, and Xe-HPG.
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    in uint gtid : SV_GroupThreadID,
    in uint gid  : SV_GroupID,
    out vertices  VertexOut verts[64],
    out indices   uint3      tris[124])
{
    SetMeshOutputCounts(64, 124);
    // ...
}
```

## Options

- `target` (string, default: `"portable"`) — one of `"portable"`, `"rdna"`, `"ada"`, `"xe-hpg"`. Selects the sweet-spot table used for the check; `"portable"` requires the value to be acceptable on all three vendors.
- `vertex-sweet-spots` (integer array, default: `[32, 64, 128]`) — overrides the built-in vertex-count sweet spot list.
- `primitive-sweet-spots` (integer array, default: `[64, 84, 124, 128]`) — overrides the built-in primitive-count sweet spot list.

## Fix availability

**suggestion** — The rule cannot rewrite the meshlet-baking pipeline; resizing meshlets requires regenerating the offline-prepared meshlet buffers. The diagnostic reports the nearest sweet-spot value for the configured target so the engine-side meshlet builder can be retuned.

## See also

- Related rule: [output-count-overrun](output-count-overrun.md) — declared output counts must not be exceeded at runtime
- AMD GPUOpen: "Mesh Shaders" best-practice guide
- NVIDIA: "Mesh Shaders" Turing/Ampere/Ada developer documentation
- DirectX 12 Ultimate: mesh shader specification (`SetMeshOutputCounts`, `[outputtopology]`)
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/meshlet-vertex-count-bad.md)

*© 2026 NelCit, CC-BY-4.0.*
