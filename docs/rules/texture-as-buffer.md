---
id: texture-as-buffer
category: texture
severity: note
applicability: suggestion
since-version: "v0.3.0"
phase: 3
---

# texture-as-buffer

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

A `Texture2D`, `Texture1D`, or `Texture2DArray` resource that is accessed exclusively through integer-coordinate `Load(int3(x, 0, 0), 0)` or `Load(int3(x, 0, 0))` calls, where the y and z coordinates are always the compile-time literal zero and the mip level is always zero. When reflection confirms that the resource is bound as a two-dimensional object but the shader never uses the second (or third) dimension, and never uses a sampler, the resource is acting as a flat linear array. The rule suggests replacing it with `Buffer<T>` for read-only access or `StructuredBuffer<T>` if the element type is a user-defined struct. It does not fire when any access uses a non-zero y coordinate, a non-zero mip level, a sampler, or `Gather` variants.

## Why it matters on a GPU

On all current GPU families (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG), a `Texture2D` resource carries metadata overhead that a `Buffer<>` does not. The hardware must maintain a surface descriptor that includes width, height, depth, mip count, sample count, tile mode, and format swizzle. Issuing a texel-fetch instruction (`image_load` on GCN/RDNA, `ld` on DXIL-SM6) to a 2D texture object instructs the TMU or the L1 texture cache to resolve a 2D address through the surface descriptor pipeline even when only one dimension varies. On RDNA 3, the image-fetch path has a fixed per-instruction overhead for descriptor decode that a raw-buffer load (`buffer_load_dword`) avoids entirely. The raw-buffer path also benefits from the scalar unit (SMEM / `s_load`) when the index is wave-uniform, reducing register pressure.

Beyond instruction cost, declaring a resource as `Texture2D` when it is logically a buffer expresses false intent to the shader compiler and to anyone reading the code. The compiler cannot apply buffer-load coalescing (merging adjacent per-thread loads into a single wider load) to texture objects, because texture loads require TMU hardware and cannot be remapped to the L2 buffer path. By contrast, `Buffer<T>` or `StructuredBuffer<T>` loads are routed through the L1/L2 data cache hierarchy, which on RDNA 3 and Ada Lovelace supports vectorised load-merging across a wave when accesses are stride-1. This can convert 32 separate 32-bit loads into a single 128-byte cache-line fill, increasing effective bandwidth utilisation from as low as 3% to near 100% for linear traversal patterns.

The `StructuredBuffer<T>` variant is preferred when the per-element type is a struct, because it preserves the element stride in the resource descriptor and makes the shader's data layout explicit. Use `Buffer<T>` for scalar or small-vector types (float, float4, uint). The rule reports the inferred element type based on the texture's declared template argument and the observed access pattern.

## Examples

### Bad

```hlsl
// Texture2D used purely as a 1D lookup table — y-coordinate is always 0.
Texture2D<float4> LookupTable : register(t0);

float4 lookup(uint index) {
    // Always loads row 0, varying only x — logically 1D.
    return LookupTable.Load(int3(index, 0, 0));
}
```

### Good

```hlsl
// Replace with Buffer<float4> — clearer intent, avoids TMU descriptor overhead,
// and enables raw-buffer coalescing in the compiler.
Buffer<float4> LookupTable : register(t0);

float4 lookup(uint index) {
    return LookupTable.Load(index);
}

// For struct element types, prefer StructuredBuffer:
struct MaterialEntry { float4 albedo; float roughness; float3 emissive; };
StructuredBuffer<MaterialEntry> Materials : register(t1);

MaterialEntry get_material(uint idx) {
    return Materials[idx];
}
```

## Options

none

## Fix availability

**suggestion** — The rule can propose a replacement declaration (`Buffer<T>` or `StructuredBuffer<T>`) and updated load call syntax. The rename must be verified by the author because it changes the resource descriptor type, which may affect root-signature or descriptor-heap layout on the CPU side. Use `// hlsl-clippy: allow(texture-as-buffer)` at the declaration site to suppress when the 2D texture declaration is required for API compatibility reasons (for example, when the same register slot is shared between an opaque 2D texture and a buffer in different shader permutations).

## See also

- Related rule: [`samplelevel-with-zero-on-mipped-tex`](samplelevel-with-zero-on-mipped-tex.md) — related pattern where the mip level is unnecessarily pinned to zero
- HLSL intrinsic reference: `Buffer<T>`, `StructuredBuffer<T>`, `Texture2D.Load` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published — will appear alongside the v0.3.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/texture-as-buffer.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
