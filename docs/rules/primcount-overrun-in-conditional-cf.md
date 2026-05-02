---
id: primcount-overrun-in-conditional-cf
category: mesh
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# primcount-overrun-in-conditional-cf

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

A mesh-shader entry point that calls `SetMeshOutputCounts(v, p)` once at the top, then issues primitive index writes (typically `outIndices[i] = ...` or `triangleIndices[i] = ...`) inside conditional control flow whose join could push the live primitive count above `p` on at least one CFG path. The rule fires when CFG analysis can prove a path exists where `i >= p` reaches a primitive write. Companion to the locked [setmeshoutputcounts-in-divergent-cf](setmeshoutputcounts-in-divergent-cf.md), which targets the *call site* of `SetMeshOutputCounts` itself; this rule targets the *writes* on the consumer side.

## Why it matters on a GPU

Mesh shaders on D3D12 (and the Vulkan / Metal equivalents) declare an upper bound on vertex and primitive output via `[outputtopology(...)]` plus a per-launch dynamic count via `SetMeshOutputCounts(maxVerts, maxPrims)`. The hardware allocates output storage for exactly that many primitives — on AMD RDNA 2/3 the mesh-shader output buffer is sized at compile time from the declared maxima and trimmed at launch by the dynamic call; on NVIDIA Ada the per-meshlet primitive ring is sized to the declared bound; on Intel Xe-HPG the equivalent output staging area is similarly sized. Writing past the declared count is undefined behaviour: the hardware may silently drop the over-count primitive, may overwrite a neighbouring meshlet's output (corrupting another lane group's geometry), or may surface a TDR / device-removed error if the IHV's runtime adds a bounds check on debug runtimes. The reproducibility varies by IHV and by driver version, which makes the bug a classic intermittent crash.

The conditional-CF flavour of the bug is particularly insidious because the `SetMeshOutputCounts` call appears once, with what looks like a safe constant or expression, and the subsequent primitive writes are guarded by per-thread conditions whose join the author does not visualise. A common shape: "if this lane's vertex passed culling, write a primitive at the next slot". When more lanes than expected pass culling, the next-slot counter exceeds the declared maximum, and the over-count writes corrupt downstream state on the GPU. The rule's CFG analysis tracks the maximum reachable write index on every path and compares against the declared count.

The fix is one of: (a) make the declared count an upper bound that all paths respect (often by passing the worst-case to `SetMeshOutputCounts` and accepting the overheard storage cost), (b) gate primitive writes on `i < p` to clip overflow, or (c) restructure the meshlet so the count is known-tight before any primitive is written (typical: do a wave-prefix-sum compaction pass to compute exact survivor count, call `SetMeshOutputCounts` with that exact count, then write). Option (c) is the highest performance but requires the most refactor.

## Examples

### Bad

```hlsl
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID,
             out vertices VertexOut verts[64],
             out indices uint3 prims[128]) {

    SetMeshOutputCounts(64, 64);   // promised at most 64 primitives

    // ... vertex writes elided ...

    // Each surviving lane writes a primitive at its own slot.
    bool pass = cull_test(gtid);
    if (pass) {
        // Up to 32 lanes may pass; if a previous iteration accumulated more
        // than 32 survivors, gtid + base_offset can exceed 64.
        uint slot = base_offset + gtid;
        prims[slot] = uint3(slot * 3, slot * 3 + 1, slot * 3 + 2);
    }
}
```

### Good

```hlsl
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void ms_main_safe(uint gtid : SV_GroupThreadID,
                  out vertices VertexOut verts[64],
                  out indices uint3 prims[128]) {

    // Wave-fold the survivor count first, then declare the exact count.
    bool pass = cull_test(gtid);
    uint survivors = WaveActiveCountBits(pass);
    SetMeshOutputCounts(64, survivors);

    // Per-lane prefix index — each survivor gets a unique in-range slot.
    uint slot = WavePrefixCountBits(pass);
    if (pass && slot < survivors) {
        prims[slot] = uint3(slot * 3, slot * 3 + 1, slot * 3 + 2);
    }
}
```

## Options

none

## Fix availability

**none** — The valid resolutions (relax the count, clip the writes, or pre-compact) all change the meshlet's output topology semantics. The diagnostic identifies the `SetMeshOutputCounts` call, the maximum-path index, and the offending primitive write so the author can choose.

## See also

- Related rule: [setmeshoutputcounts-in-divergent-cf](setmeshoutputcounts-in-divergent-cf.md) — call-site phrasing of the same family
- Related rule: [output-count-overrun](output-count-overrun.md) — generic mesh-output overrun
- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — paired mesh-output completeness rule
- HLSL reference: mesh-shader specification, `SetMeshOutputCounts` semantics
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/primcount-overrun-in-conditional-cf.md)

*© 2026 NelCit, CC-BY-4.0.*
