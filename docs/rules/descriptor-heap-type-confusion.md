---
id: descriptor-heap-type-confusion
category: bindings
severity: error
applicability: none
since-version: v0.5.0
phase: 3
---

# descriptor-heap-type-confusion

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

An SM 6.6 dynamic heap access where the descriptor heap type does not match the resource type being requested: a sampler resource fetched from `ResourceDescriptorHeap` (the CBV/SRV/UAV heap), or a non-sampler resource (texture, buffer, CBV) fetched from `SamplerDescriptorHeap`. The rule uses Slang's type reflection to determine the declared type of the variable receiving the heap access (e.g., `SamplerState`, `SamplerComparisonState` vs `Texture2D`, `Buffer<>`, `ConstantBuffer<>`) and compares it against which heap object is used. Both `ResourceDescriptorHeap` and `SamplerDescriptorHeap` are accessible from the same shader source, but they index into physically separate heap arrays; reading across the boundary is undefined behaviour per the D3D12 specification.

## Why it matters on a GPU

D3D12 descriptor heaps come in two distinct physical types: `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` (for constant buffer views, shader resource views, and unordered access views) and `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER` (for samplers). These are separate memory regions in driver-managed GPU-accessible memory. At the hardware level — RDNA, Turing, Xe-HPG — the descriptor table pointer that the driver uses to locate a sampler lives in a different register from the one that locates CBVs/SRVs/UAVs. When HLSL accesses `SamplerDescriptorHeap[i]` and casts the result to a `Texture2D`, the driver forwards the sampler-heap base address to a resource-descriptor load unit; that unit interprets sampler-heap binary data as a resource descriptor. The result is a garbage resource handle.

The failure mode is severe: the GPU may attempt to read memory that is not a valid resource descriptor, potentially reading from an unmapped GPU virtual address. On modern D3D12 implementations, GPU page faults from bad descriptor reads in release mode often manifest as device-removed (TDR) crashes, which are among the hardest GPU bugs to diagnose. The DirectX Debug Layer catches this at validation time, but release builds run without the validation layer. Because the symptom (TDR or undefined rendering) is disconnected from the source (a wrong heap name), the fix is not obvious without a linter.

No automated fix is offered because the repair may require restructuring which descriptors are placed into which heap, updating root signature descriptor table declarations, and adjusting CPU-side descriptor copy calls — all changes that span multiple files and require understanding the full resource binding layout of the application.

## Examples

### Bad

```hlsl
// Fetching a SamplerState from the CBV/SRV/UAV heap — spec-undefined behaviour.
SamplerState get_sampler(uint sampler_index) {
    // ResourceDescriptorHeap is the CBV/SRV/UAV heap; SamplerState lives
    // in SamplerDescriptorHeap. This is a descriptor-heap type confusion.
    return ResourceDescriptorHeap[sampler_index];  // HIT(descriptor-heap-type-confusion)
}

// Fetching a Texture2D from the sampler heap — equally wrong.
Texture2D<float4> get_texture_wrong(uint tex_index) {
    return SamplerDescriptorHeap[tex_index];  // HIT(descriptor-heap-type-confusion)
}
```

### Good

```hlsl
// Correct: samplers from SamplerDescriptorHeap, resources from ResourceDescriptorHeap.
SamplerState get_sampler(uint sampler_index) {
    return SamplerDescriptorHeap[NonUniformResourceIndex(sampler_index)];
}

Texture2D<float4> get_texture(uint tex_index) {
    return ResourceDescriptorHeap[NonUniformResourceIndex(tex_index)];
}
```

## Options

none

## Fix availability

**none** — Correcting a descriptor-heap type mismatch requires understanding the full D3D12 root signature and heap layout. Changing `ResourceDescriptorHeap` to `SamplerDescriptorHeap` (or vice versa) in the HLSL source is only half the fix; the CPU side must also place descriptors into the correct heap. An automated rewrite of just the HLSL side would produce apparently-fixed source that still crashes at runtime if the CPU side is not updated in concert.

## See also

- Related rule: [descriptor-heap-no-non-uniform-marker](descriptor-heap-no-non-uniform-marker.md) — divergent heap index missing `NonUniformResourceIndex`
- Related rule: [non-uniform-resource-index](non-uniform-resource-index.md) — missing `NonUniformResourceIndex` on array-of-texture parameters
- D3D12 specification: `D3D12_DESCRIPTOR_HEAP_TYPE`, descriptor heap creation, SM 6.6 heap indexing
- DirectX-Specs: Shader Model 6.6 — Direct Heap Access — heap type requirements
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/descriptor-heap-type-confusion.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
