---
id: output-count-overrun
category: mesh
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# output-count-overrun

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Mesh-shader entry points that, at the IR level, write to `verts[i]` or `tris[i]` with an index `i` whose statically known upper bound exceeds the corresponding declared `out vertices N` / `out indices M` capacity. The check also fires on the matching `SetMeshOutputCounts(v, p)` call when `v > N` or `p > M` for compile-time-constant arguments. The analysis runs after Slang's IR loop-bound and range propagation passes so it can resolve `for (uint i = 0; i < gtid + 1; ++i) verts[i] = ...` patterns where the index is bounded by a thread-id derived expression.

## Why it matters on a GPU

The mesh-shader output arrays are not ordinary HLSL arrays — they are bound to fixed-size on-chip groupshared regions whose extent is fixed at PSO compilation time from the `out vertices N` and `out indices M` attributes. On AMD RDNA 2/3, this region lives in the LDS (local data store); writing past the declared count typically clobbers neighbouring meshlet output state or, depending on driver and shader compiler version, silently writes nothing while the rasteriser consumes the truncated output. On NVIDIA Ada Lovelace, the same scenario produces undefined behaviour: the SM's mesh-shader output buffer has bank-conflict checks, but no bounds checks, and overruns can corrupt index data fed to the raster. Intel Xe-HPG validates only in development driver builds; release drivers permit the overrun and the resulting visual artefacts are typically diagnosed days later as "missing triangles" or "geometry pop-in."

The runtime cost matters too. A mesh shader that declares `out vertices 256` but only ever writes 64 still pre-allocates the LDS for 256 vertices, reducing the number of mesh-shader waves that can fit per CU/SM. Conversely, a mesh shader that declares `out vertices 64` but writes up to 65 (the canonical off-by-one) corrupts a single vertex slot belonging to a sibling meshlet — typically invisible until the camera angle changes and the corruption lands on a foreground triangle. Both cases are silent, both are common, and both are detectable from IR before the shader ships.

Static detection avoids two failure modes that the runtime cannot. First, the DirectX 12 debug layer only catches the overrun if the validation layer is active and the pixel happens to be drawn; production builds skip validation. Second, the GPU-based validation layer catches it but at a 10-100x slowdown, so it is rarely enabled outside CI smoke runs. A linter pass on the compiled IR is fast and runs on every build, catching off-by-one errors and `gtid + 1`-style mistakes before the shader hits the runtime at all.

## Examples

### Bad

```hlsl
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    in uint gtid : SV_GroupThreadID,
    out vertices VertexOut verts[64],
    out indices  uint3     tris[128])
{
    SetMeshOutputCounts(64, 128);
    // Off-by-one: writes verts[0..64], one past the declared bound of 64.
    for (uint i = 0; i <= gtid; ++i) {
        verts[i] = ComputeVertex(i);
    }
}
```

### Good

```hlsl
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MeshMain(
    in uint gtid : SV_GroupThreadID,
    out vertices VertexOut verts[64],
    out indices  uint3     tris[128])
{
    SetMeshOutputCounts(64, 128);
    // Strict inequality keeps the index in [0, 64).
    for (uint i = 0; i < gtid + 1u && i < 64u; ++i) {
        verts[i] = ComputeVertex(i);
    }
}
```

## Options

- `assume-loop-bounds` (bool, default: `true`) — use Slang IR loop-bound analysis to resolve loop indices. Set to `false` to fire only on direct constant-index overruns, suppressing the loop-derived diagnostics.
- `treat-equal-as-overrun` (bool, default: `true`) — flag `i <= N` patterns where `N` is the declared array size; mesh-shader arrays are zero-indexed so the inclusive upper bound is the off-by-one trap.

## Fix availability

**suggestion** — The rule pinpoints the offending index expression and the declared bound it exceeds. The correct fix (clamp the loop, change the declared count, or restructure the meshlet) depends on author intent and is left to the developer.

## See also

- Related rule: [meshlet-vertex-count-bad](meshlet-vertex-count-bad.md) — choosing the declared count well in the first place
- DirectX 12 Ultimate: mesh shader specification, `SetMeshOutputCounts` semantics
- AMD GPUOpen: "Mesh Shaders" — LDS layout and output-array sizing
- NVIDIA developer documentation: mesh-shader output-buffer behaviour on Ada Lovelace
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/output-count-overrun.md)

*© 2026 NelCit, CC-BY-4.0.*
