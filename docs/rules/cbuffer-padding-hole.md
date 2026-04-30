---
id: cbuffer-padding-hole
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# cbuffer-padding-hole

> **Status:** pre-v0 — rule scheduled for Phase 3; see [ROADMAP](../../ROADMAP.md).

## What it detects

A `cbuffer` or `ConstantBuffer<T>` declaration whose member layout contains one or more implicit padding holes: gaps of unused bytes inserted by the HLSL packing rules (HLSL pack rule: each member is aligned to the smaller of its own size or 16 bytes, and no member may straddle a 16-byte boundary). The rule uses Slang's reflection API to retrieve the byte offset of every member, computes the gaps between consecutive members, and fires when any gap is non-zero. Common examples: `float Time` (4 bytes) followed by `float3 LightDir` (12 bytes, 16-byte aligned) leaves a 12-byte hole at offsets 4–15 (see `tests/fixtures/phase3/bindings.hlsl`, line 3–10).

## Why it matters on a GPU

cbuffer reads on all current GPU hardware — AMD RDNA/RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace — travel through the scalar/constant-data path. The hardware delivers constant data in 16-byte register slots (four `float` registers, one `float4`). A single cbuffer fetch retrieves one or more complete 128-bit slots; there is no mechanism to fetch a sub-register fraction. When padding holes exist, each 16-byte slot that contains a hole contains less usable data than it could: the padding bytes are transmitted across the constant-bus, occupy space in the L1 constant cache, and are then discarded by the shader.

On AMD RDNA 3 the scalar data cache (K-cache) holds 16 KB per CU and is fully shared across all in-flight waves on that CU. Every cache line wasted on padding bytes is a cache line that cannot hold data from a different cbuffer or a different field. In a deferred-lighting pass where every wave in every CU loads the same per-frame `FrameConstants` cbuffer, a 12-byte hole means the first 16-byte slot carries only one `float` of real data — the equivalent of 75% wasted cache bandwidth on that slot. Across hundreds of CUs executing millions of threads per frame, padding holes are a silent tax on the constant-data path.

The fix requires reordering members from largest to smallest natural alignment, or grouping scalar members together. The rewriter can propose the sorted layout but cannot apply it automatically: reordering cbuffer members changes the `packoffset` offsets and may affect CPU-side code that fills the buffer using explicit byte offsets or layout reflection. The diagnostic names the hole location (byte offset and size) so the author can act immediately.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 3-10
// HIT(cbuffer-padding-hole): Time (4B) followed by LightDir (16B-aligned)
// leaves a 12-byte padding hole at offsets 4..15.
cbuffer FramePadded : register(b0) {
    float    Time;      // offset 0, size 4
    // 12-byte hole at offsets 4..15 — LightDir is float3 (12B) and
    // must start on a 16-byte boundary.
    float3   LightDir;  // offset 16, size 12
    float    Exposure;  // offset 28, size 4
    float4x4 ViewProj;  // offset 48, size 64
};
```

### Good

```hlsl
// Reorder to eliminate the hole — scalars grouped at the end of a 16-byte slot.
cbuffer FramePacked : register(b0) {
    float3   LightDir;  // offset 0,  size 12
    float    Time;      // offset 12, size 4  — fills the same slot
    float    Exposure;  // offset 16, size 4
    float3   _pad;      // explicit pad, or pack Exposure into previous slot
    float4x4 ViewProj;  // offset 32, size 64
};

// Better: pack all scalars into one float4 slot.
cbuffer FrameBest : register(b0) {
    float4x4 ViewProj;               // offset 0,  size 64
    float3   LightDir;               // offset 64, size 12
    float    Time;                   // offset 76, size 4   — same 16B slot
    float2   ExposureAndPad;         // offset 80, size 8
};
```

## Options

none

## Fix availability

**suggestion** — The fix involves reordering or repacking struct members, which may invalidate CPU-side layout assumptions (e.g., `memcpy` fills, explicit `packoffset` annotations, or C++ mirror structs). `hlsl-clippy fix` generates a candidate reordered layout as a comment but does not apply it automatically.

## See also

- Related rule: [bool-straddles-16b](bool-straddles-16b.md) — `bool` packed across the 16-byte register boundary
- Related rule: [oversized-cbuffer](oversized-cbuffer.md) — cbuffer exceeds the default 4 KB threshold
- Related rule: [cbuffer-fits-rootconstants](cbuffer-fits-rootconstants.md) — small cbuffer should be root constants on D3D12
- HLSL constant buffer packing rules: `packoffset` keyword documentation in the DirectX HLSL reference
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/cbuffer-padding-hole.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
