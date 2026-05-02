---
id: mesh-output-decl-exceeds-256
category: mesh
severity: error
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# mesh-output-decl-exceeds-256

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

A mesh-shader entry point whose `out vertices` or `out indices` array declarations exceed 256 elements in either dimension. The D3D12 mesh-pipeline specification caps both per-group output declarations at 256: a maximum of 256 vertices and 256 primitives. The rule constant-folds the array size literals on the `out vertices` / `out indices` parameters and fires when either exceeds 256 on a function annotated `[shader("mesh")]`.

## Why it matters on a GPU

Mesh-shader output buffers live in a fixed per-group slot allocated by the pipeline at workgroup launch time. On NVIDIA Turing and Ada Lovelace, the per-group output region is sized for 256 vertices and 256 primitives multiplied by the configured per-vertex output stride. On AMD RDNA 2/3, the mesh shader writes through a primitive-shader pipeline that uses an LDS-resident output region carved at the same cap. Intel Xe-HPG (Arc Alchemist, Battlemage) implements the same 256/256 ceiling as part of its mesh-pipeline conformance to the D3D12 spec.

Declaring more than 256 of either is a hard PSO-creation failure: `D3D12CreateGraphicsPipelineState` returns `E_INVALIDARG`. As with `mesh-numthreads-over-128`, catching this at lint time replaces a runtime "your PSO won't compile" error with a source-located diagnostic. The diagnostic includes the actual declared count so the author knows the magnitude of the over-shoot.

The fix is to reduce the meshlet size: typical production meshlets target 64 vertices / 124 primitives (the meshoptimizer convention) or 128 vertices / 128 primitives (the NVIDIA-recommended starting point). Larger nominal output capacities almost never improve culling effectiveness and waste output-region memory, which translates to lower wave occupancy on RDNA 2/3 because the LDS allocation per group goes up.

## Examples

### Bad

```hlsl
// 512 vertices — over the 256 cap. PSO creation fails.
[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint tid : SV_GroupThreadID,
          out vertices Vertex verts[512], // ERROR: exceeds 256 cap
          out indices  uint3  tris[124])
{
    /* ... */
}
```

### Good

```hlsl
// 64 verts / 124 prims — the meshoptimizer convention; well-supported on
// every IHV and leaves enough LDS headroom for high wave occupancy.
[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void main(uint tid : SV_GroupThreadID,
          out vertices Vertex verts[64],
          out indices  uint3  tris[124])
{
    /* ... */
}
```

## Options

none

## Fix availability

**none** — Reducing the cap changes the meshlet packing and is a content-side decision. The diagnostic names the offending dimension(s) so the author can pick a new shape.

## See also

- Related rule: [mesh-numthreads-over-128](mesh-numthreads-over-128.md) — companion mesh-pipeline cap on thread count
- Related rule: [meshlet-vertex-count-bad](meshlet-vertex-count-bad.md) — meshlet shape that under-utilises the slot budget
- Related rule: [as-payload-over-16k](as-payload-over-16k.md) — amplification-payload cap
- D3D12 specification: Mesh Shader output cap (256 vertices, 256 primitives)
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mesh-output-decl-exceeds-256.md)

*© 2026 NelCit, CC-BY-4.0.*
