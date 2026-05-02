---
id: mesh-numthreads-over-128
category: mesh
severity: error
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# mesh-numthreads-over-128

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

A mesh-shader or amplification-shader entry point whose `[numthreads(X, Y, Z)]` attribute multiplies out to more than 128 threads per group. The D3D12 mesh-pipeline specification caps the per-group thread count at 128 for both stages; values above the cap fail PSO creation. The rule constant-folds the three integer arguments and fires when `X * Y * Z > 128` on a function annotated `[shader("mesh")]` or `[shader("amplification")]`.

## Why it matters on a GPU

Mesh and amplification shaders run on the same compute-style backend used for compute shaders, but the pipeline reserves a specific resource budget per workgroup: per-workgroup payload memory (16 KB for AS), per-workgroup vertex/primitive output memory (the output declaration cap), and a thread cap chosen so the whole pipeline can guarantee in-order delivery to the rasterizer. On NVIDIA Turing and Ada Lovelace, the mesh/AS dispatch path uses a fixed-size scoreboard slot per group; on AMD RDNA 2/3, the mesh shader runs as a primitive-shader-style workgroup that the rasterizer drains in lockstep; on Intel Xe-HPG, the pipeline budgets a per-group launch quantum sized to the 128-thread cap. The 128 ceiling is the contract that all three IHVs and the D3D12 runtime agreed on.

Exceeding the cap is not a perf footgun — it's a hard validation failure. `D3D12CreateGraphicsPipelineState` returns `E_INVALIDARG` and the PSO is never created. Catching this at lint time replaces a confusing runtime error with a precise source-location diagnostic, which is the friendlier failure mode.

The fix is to either reduce the per-axis dimensions (most mesh shaders are written `[numthreads(64, 1, 1)]` or `[numthreads(128, 1, 1)]` to maximise wave occupancy on RDNA 2-3 wave64 and on NVIDIA wave32 respectively) or split the work across multiple AS dispatches.

## Examples

### Bad

```hlsl
// 256 threads — over the 128 cap. PSO creation fails.
[shader("mesh")]
[numthreads(16, 16, 1)]
[outputtopology("triangle")]
void main(uint tid : SV_GroupThreadID,
          out vertices Vertex   verts[64],
          out indices  uint3    tris[124])
{
    /* ... */
}
```

### Good

```hlsl
// 128 threads — at the cap. Most mesh shaders stop at 64 or 128 to map
// cleanly onto wave32 or wave64.
[shader("mesh")]
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void main(uint tid : SV_GroupThreadID,
          out vertices Vertex   verts[64],
          out indices  uint3    tris[124])
{
    /* ... */
}
```

## Options

none

## Fix availability

**none** — Reducing the thread count changes how the meshlet is decomposed; the rule cannot pick the right new shape automatically. The diagnostic names the offending product so the author can refactor.

## See also

- Related rule: [mesh-output-decl-exceeds-256](mesh-output-decl-exceeds-256.md) — output declaration cap (256 verts / 256 prims)
- Related rule: [as-payload-over-16k](as-payload-over-16k.md) — amplification-payload cap (16 KB)
- Related rule: [meshlet-vertex-count-bad](meshlet-vertex-count-bad.md) — meshlet shape that under-utilises the vertex slot budget
- D3D12 specification: Mesh Shader thread-group size limits (SM 6.5)
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mesh-numthreads-over-128.md)

*© 2026 NelCit, CC-BY-4.0.*
