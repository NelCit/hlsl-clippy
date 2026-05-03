---
id: setmeshoutputcounts-in-divergent-cf
category: mesh
severity: error
applicability: none
since-version: v0.4.0
phase: 4
language_applicability: ["hlsl"]
---

# setmeshoutputcounts-in-divergent-cf

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls to `SetMeshOutputCounts(vertexCount, primitiveCount)` inside a mesh shader that are reachable from a non-thread-uniform branch (any `if`, `for`, `while`, or `switch` whose predicate depends on `SV_GroupThreadID`, `SV_DispatchThreadID`, or any per-thread varying value), or `SetMeshOutputCounts` calls that can execute more than once per shader invocation along any control-flow path. Both forms are explicit undefined behaviour in the D3D12 mesh-shader specification.

## Why it matters on a GPU

A mesh shader replaces the traditional vertex/hull/domain/geometry pipeline with a single compute-style entry point that produces a small, bounded array of vertices and primitives. `SetMeshOutputCounts` tells the runtime how many of each will actually be written. The hardware uses this count to allocate output buffers downstream (parameter cache, primitive setup, attribute interpolation), to set up the rasteriser's primitive walker, and to issue fixed-function clip / cull work. The contract requires the call to happen exactly once, in thread-uniform control flow, before any `SetMeshOutputs` or `SetMeshPrimitives` write. Calling it from divergent control flow means different threads in the group disagree on the output sizing; calling it twice means the second call's values overwrite the first while output writes may have already started against the original allocation.

On AMD RDNA 2/3, mesh shaders execute on the same SIMD lanes as compute, but the mesh-output stage (NGG culling and parameter cache write) consumes the count from a fixed register set written by the `SetMeshOutputCounts` intrinsic. If different lanes write different values, the hardware-defined behaviour is to take whichever value the highest-numbered active lane wrote — which is not what the shader author intended. On NVIDIA Turing and Ada with task/mesh shaders, the count is committed via a dedicated MNG (mesh next-gen) handshake, and divergent writes silently corrupt the count or produce a hung output stage. On Intel Xe-HPG, the analogous mesh-output unit reads the count from a uniform broadcast slot and a non-uniform write produces undefined output.

Driver-level diagnostics rarely catch this. The shader compiles cleanly, the validator does not flag the call (it cannot easily prove uniformity statically), the PIX / RGP / Nsight captures show the mesh shader running, and the symptom is missing triangles, garbage primitive indices, or a TDR after the rasteriser tries to walk a count larger than the actual output array. Because the bug surfaces only when input data takes a particular shape (the divergent branch is rarely taken in test data), it is a class of latent crash that ships in production. The fix pattern is to compute the output sizing once with a wave-level reduction (`WaveActiveSum` of per-thread output counts, then `WaveReadLaneFirst` to broadcast), then call `SetMeshOutputCounts` in the unconditional prologue of the shader before any branching.

## Examples

### Bad

```hlsl
struct MeshVertex { float4 pos : SV_Position; };

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_divergent_count(
    uint gtid : SV_GroupThreadID,
    out vertices  MeshVertex verts[64],
    out indices   uint3      tris[42])
{
    if (gtid == 0) {
        // SetMeshOutputCounts called from a single-thread branch — UB.
        // Other threads never execute this and the output stage receives
        // an undefined count.
        SetMeshOutputCounts(64, 42);
    }
    // ... vertex / primitive emission
}
```

### Good

```hlsl
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_uniform_count(
    uint gtid : SV_GroupThreadID,
    out vertices  MeshVertex verts[64],
    out indices   uint3      tris[42])
{
    // Unconditional, thread-uniform call before any branch. All threads
    // execute exactly the same intrinsic with the same arguments.
    SetMeshOutputCounts(64, 42);

    // Per-thread emission proceeds normally afterwards.
    if (gtid < 64) {
        verts[gtid].pos = float4(0, 0, 0, 1);
    }
    if (gtid < 42) {
        tris[gtid] = uint3(0, 1, 2);
    }
}
```

## Options

none

## Fix availability

**none** — Hoisting `SetMeshOutputCounts` to uniform control flow requires reasoning about the algorithm: the count must reflect the maximum work the threads will do, possibly computed via a wave reduction of per-thread predicates. The diagnostic identifies the call site and the enclosing non-uniform construct (or a second call site reachable on the same path).

## See also

- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — the same divergent-CF family for compute barriers
- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — work-graph node output completion (analogous in mesh nodes)
- Microsoft DirectX docs: Mesh Shaders — `SetMeshOutputCounts` contract
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/setmeshoutputcounts-in-divergent-cf.md)

*© 2026 NelCit, CC-BY-4.0.*
