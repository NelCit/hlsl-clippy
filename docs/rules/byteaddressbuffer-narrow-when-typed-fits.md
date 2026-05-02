---
id: byteaddressbuffer-narrow-when-typed-fits
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# byteaddressbuffer-narrow-when-typed-fits

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

A `ByteAddressBuffer.Load4` (or `Load2` / `Load3`) followed by an `asfloat` / `asuint` round-trip that yields a POD whose size and layout exactly match a `Buffer<float4>`, `Buffer<uint4>`, or `StructuredBuffer<T>` view available against the same resource. The detector uses Slang reflection to recognise the binding's underlying resource and matches the local POD shape produced by the round-trip against typed-view candidates with identical stride. It does not fire when the bytes are reinterpreted in a way that no typed view supports (e.g. mixed `uint16_t` and `uint32_t` lanes within the same load).

## Why it matters on a GPU

`ByteAddressBuffer` and typed buffer views travel through different cache paths on every modern desktop IHV. On AMD RDNA 2/3, `ByteAddressBuffer` accesses go through the K$/scalar L1 path because the byte addressing exposes raw memory; `Buffer<float4>` and `StructuredBuffer<T>` are bound through the texture descriptor path, which routes through the V$/texture L1 with format-aware widening hardware. On NVIDIA Turing and Ada Lovelace, typed loads use the texture/L1 unified cache with format converters in the load pipeline, while raw byte loads go through the LD/ST units against the generic L1. Intel Xe-HPG draws a similar distinction between the typed sampler/L1 path and the URB-style raw load.

When the access pattern is a regular stride of a typed POD (vec4 of float, vec4 of uint, structured records), the typed path is the engineered fast path: address computation collapses into the descriptor stride, the format converter handles `UNORM`/`FLOAT`/`UINT` packing for free, and the cache prefetcher recognises the regular stride. Routing those same accesses through `ByteAddressBuffer` defeats the format converter, runs the address arithmetic in shader ALU instead of the descriptor unit, and hits the wrong L1 partition for the common case (RDNA's K$ is sized for scalar uniform constants, not vec4 streams).

The mirror case also matters: `ByteAddressBuffer` is the right call when the access pattern is irregular, when the data is a sparse mix of bit-packed fields, or when the resource binds DXIL/SPIR-V intrinsics that need raw byte access. The rule fires only when reflection confirms a typed view *exists* and the round-trip produces exactly that POD — i.e. the developer is paying the wrong-cache cost for no benefit.

## Examples

### Bad

```hlsl
ByteAddressBuffer Vertices : register(t0);

float4 read_position(uint vertex_index) {
    // Round-trip Load4 + asfloat to recover float4 — exact POD match for
    // Buffer<float4> against the same binding.
    uint4 raw = Vertices.Load4(vertex_index * 16);
    return asfloat(raw);
}
```

### Good

```hlsl
// Bind the same resource as a typed view; the texture cache path handles
// the regular vec4 stride at descriptor speed.
Buffer<float4> Vertices : register(t0);

float4 read_position(uint vertex_index) {
    return Vertices[vertex_index];
}
```

## Options

none

## Fix availability

**suggestion** — Switching the binding type may require corresponding changes on the C++ root-signature / descriptor side, and may interact with other call sites that rely on the byte-addressed view. The diagnostic shows the candidate typed view and leaves the binding decision to the author.

## See also

- Related rule: [byteaddressbuffer-load-misaligned](byteaddressbuffer-load-misaligned.md) — alignment hazard for raw widened loads
- Related rule: [structured-buffer-stride-not-cache-aligned](structured-buffer-stride-not-cache-aligned.md) — stride choice for typed structured buffers
- HLSL intrinsic reference: `Buffer<T>`, `StructuredBuffer<T>`, `ByteAddressBuffer` resource type documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/byteaddressbuffer-narrow-when-typed-fits.md)

*© 2026 NelCit, CC-BY-4.0.*
