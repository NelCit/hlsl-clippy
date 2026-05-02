---
id: descriptor-heap-no-non-uniform-marker
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# descriptor-heap-no-non-uniform-marker

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `ResourceDescriptorHeap[i]` or `SamplerDescriptorHeap[i]` access (SM 6.6 dynamic indexing into the descriptor heap) where the index `i` is a per-lane divergent value — typically a function parameter, a semantic input (`TEXCOORD`, `SV_InstanceID`), or any value derived from one — and the index is not wrapped in `NonUniformResourceIndex(...)`. The rule uses Slang's reflection and uniformity analysis to determine whether `i` could differ across lanes in the same wave. See `tests/fixtures/phase3/bindings_extra.hlsl`, lines 30–32 for the `get_material_texture` example and lines 35–38 for the correct `NonUniformResourceIndex` counterpart.

## Why it matters on a GPU

SM 6.6 introduced direct `ResourceDescriptorHeap` indexing as a first-class HLSL feature. A heap index is said to be non-uniform when its value may differ across lanes of the same wave (i.e., it is per-lane divergent, not wave-uniform). The HLSL and DXIL specifications require that any non-uniform heap index be wrapped in `NonUniformResourceIndex` to signal this to the driver and hardware. Without the marker, the behaviour is explicitly undefined by the specification.

In practice, on AMD RDNA hardware the driver emits a wave-uniform resource load by default: all lanes in the wave use the index value from lane 0. Lanes whose divergent index differs from lane 0's value silently receive the wrong descriptor — they sample from the wrong texture, with no GPU-side error or validation-layer warning in release mode. The resulting rendering artefacts (wrong textures on divergent geometry) are extremely difficult to diagnose because the failure is invisible when viewed from a single lane's perspective.

When `NonUniformResourceIndex` is present, the driver is required to emit a correct (if potentially expensive) implementation. On RDNA, this typically becomes a waterfall loop: the wave executes once per unique index value among the active lanes, with inactive lanes masked. This is correct but slow — in the worst case (all 64 lanes have unique texture indices) it executes the descriptor load 64 times sequentially. The rule fires as `warn` rather than `error` because the waterfall path, while slow, is at least correct; the missing-marker path is undefined behaviour. Prefer grouping draw calls to maximise wave-uniform indices where possible, and apply `NonUniformResourceIndex` whenever the index is genuinely per-lane.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings_extra.hlsl, lines 30-32
// HIT(descriptor-heap-no-non-uniform-marker): tex_index is a per-lane function
// parameter; without NonUniformResourceIndex the access is spec-UB.
Texture2D<float4> get_material_texture(uint tex_index) {
    return ResourceDescriptorHeap[tex_index];
}
```

### Good

```hlsl
// From tests/fixtures/phase3/bindings_extra.hlsl, lines 35-38
// SHOULD-NOT-HIT(descriptor-heap-no-non-uniform-marker): correctly marked.
Texture2D<float4> get_material_texture_safe(uint tex_index) {
    return ResourceDescriptorHeap[NonUniformResourceIndex(tex_index)];
}

// If the index is provably wave-uniform (e.g., loaded from a root constant
// that is the same for all invocations), no marker is needed:
Texture2D<float4> get_uniform_texture(uint uniform_tex_index) {
    // uniform_tex_index comes from a root constant — same for all lanes.
    return ResourceDescriptorHeap[uniform_tex_index];  // no HIT
}
```

## Options

none

## Fix availability

**suggestion** — Wrapping the index in `NonUniformResourceIndex(...)` is a one-line textual change, but `hlsl-clippy` cannot be certain the index is divergent in all call contexts (it may be uniform at some call sites). The fix is suggested with a note that it is correct to add the marker even on uniform indices (it may slow uniform paths slightly on some drivers, but it is never incorrect).

## See also

- Related rule: [non-uniform-resource-index](non-uniform-resource-index.md) — missing `NonUniformResourceIndex` on array-of-texture resource parameters
- Related rule: [descriptor-heap-type-confusion](descriptor-heap-type-confusion.md) — sampler fetched from the CBV/SRV/UAV heap or vice versa
- HLSL SM 6.6 specification: `ResourceDescriptorHeap`, `SamplerDescriptorHeap`, `NonUniformResourceIndex`
- DirectX-Specs: Shader Model 6.6 — Direct Heap Access
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/descriptor-heap-no-non-uniform-marker.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
