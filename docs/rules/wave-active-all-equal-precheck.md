---
id: wave-active-all-equal-precheck
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# wave-active-all-equal-precheck

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Resource-array indexing patterns of the form `Textures[idx].Sample(...)` or `Buffers[idx].Load(...)` where `idx` is computed from per-thread data (instance ID, material ID buffered by `SV_VertexID`, dispatch thread ID) and the same shader does not first issue a `WaveActiveAllEqual(idx)` test to take a single uniform path when the wave happens to converge on one value. The rule also fires for indexed `cbuffer` array reads and for `NonUniformResourceIndex(idx)` calls that lack the precheck. Tile-based deferred renderers, GPU-driven culling pipelines, and bindless material systems are the typical sources.

## Why it matters on a GPU

When a shader fetches from a resource array with a per-thread index, the hardware must perform one descriptor table walk per unique index in the wave. On AMD RDNA 2/3, divergent descriptor access uses the `S_LOAD_DWORDX4` cascade where the scalar unit scalarises the descriptor read across unique indices in the wave: 32 distinct indices means 32 sequential 16-byte descriptor loads, each blocking the wave on the L0 K$ cache. On NVIDIA Turing and Ada, divergent bindless access goes through the texture descriptor cache (TDC) and serialises similarly across unique handles. On Intel Xe-HPG, the bindless heap is read through the surface state heap with per-unique-handle BTI lookups. The throughput cost grows linearly with the number of unique indices in the wave, with the worst case at 32-64x the cost of a uniform fetch.

In practice, many "non-uniform" indices are uniform in most waves. A material ID buffer per-instance often ends up with all 32 lanes of a single-instance triangle hitting the same material; a cluster ID computed from screen tile coordinates is constant within a wave that spans one tile; a light index from a tiled cluster is uniform inside the cluster. `WaveActiveAllEqual(idx)` is a cheap wave intrinsic — one `v_cmp_eq` plus a wave reduction, roughly 2-4 cycles on RDNA — that lets the shader detect the uniform case at runtime and dispatch to a fast path that uses `WaveReadLaneFirst(idx)` to scalarise the descriptor for the entire wave. The slow path falls back to `NonUniformResourceIndex(idx)` only when the wave is genuinely divergent.

The benefit is highly content-dependent but routinely measured at 20-60% wall-clock improvement on bindless material passes, GPU-driven indirect draws, and clustered-light shading. When the wave is uniform the fast path takes one descriptor load instead of one-per-lane; when the wave is divergent the precheck adds only a few cycles before falling back to the same code that would have run anyway. The pattern is sometimes called "scalarisation hint" or "wave coherent fast path"; it appears in driver headers (AMD's GPU Open shader-extension samples) and engine source (Unreal Engine 5's `Strata` material model and the bindless texture path in id Tech 7).

## Examples

### Bad

```hlsl
Texture2D     MaterialTextures[1024] : register(t0);
SamplerState  LinearSampler          : register(s0);
StructuredBuffer<uint> MaterialIds   : register(t1);

float4 ps_bindless(uint instance : SV_InstanceID, float2 uv : TEXCOORD0) : SV_Target {
    uint idx = MaterialIds[instance];
    // Treated as fully divergent — descriptor walk runs per unique idx in the wave.
    return MaterialTextures[NonUniformResourceIndex(idx)].Sample(LinearSampler, uv);
}
```

### Good

```hlsl
float4 ps_bindless_scalarised(uint instance : SV_InstanceID, float2 uv : TEXCOORD0) : SV_Target {
    uint idx = MaterialIds[instance];
    if (WaveActiveAllEqual(idx)) {
        // Fast path: the whole wave agrees on idx. Read it as a scalar and
        // do one descriptor load for the wave.
        uint scalar_idx = WaveReadLaneFirst(idx);
        return MaterialTextures[scalar_idx].Sample(LinearSampler, uv);
    }
    // Slow path: genuinely divergent — fall back to the per-lane path.
    return MaterialTextures[NonUniformResourceIndex(idx)].Sample(LinearSampler, uv);
}
```

## Options

none

## Fix availability

**suggestion** — Adding the `WaveActiveAllEqual` precheck is a clear performance optimisation but introduces a new branch and changes the shader's compiled DXIL/SPIR-V structure. Whether it pays off depends on the empirical wave coherence of the workload. The diagnostic identifies the indexed-resource access and proposes the wrapping pattern.

## See also

- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — wave intrinsics in divergent CF (the slow-path side)
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — helper-lane participation in wave operations
- HLSL intrinsic reference: `WaveActiveAllEqual`, `WaveReadLaneFirst`, `NonUniformResourceIndex` in the DirectX HLSL Intrinsics documentation
- AMD GPU Open: scalarisation patterns in bindless rendering
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wave-active-all-equal-precheck.md)

*© 2026 NelCit, CC-BY-4.0.*
