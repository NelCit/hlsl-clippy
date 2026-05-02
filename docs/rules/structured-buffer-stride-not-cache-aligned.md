---
id: structured-buffer-stride-not-cache-aligned
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# structured-buffer-stride-not-cache-aligned

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `StructuredBuffer<T>` or `RWStructuredBuffer<T>` whose element stride (computed by Slang reflection from the HLSL packing rules applied to `T`) is a multiple of 4 but not a multiple of the configured cache-line target (default 32 bytes; configurable to 16, 32, 64, or 128). The detector uses reflection rather than an AST scan because the actual stride includes implicit padding the source layout does not show. It does not fire on stride 16, 32, 64, 128 etc.; it fires on strides like 12, 20, 24, 28, 36, 40, 48 (multiple of 4 but co-prime with the cache-line target divided by 4).

## Why it matters on a GPU

GPU L1 caches are line-organised. AMD RDNA 2/3 vector L1 is 64-byte lines; NVIDIA Turing/Ada L1 is 128-byte lines; Intel Xe-HPG is 64-byte lines. When a wave loads element `i` and element `i+1` from a `StructuredBuffer<T>`, the two loads hit the same cache line only if `stride * 2 <= line_size` and the elements are contiguous within a line. A stride of 24 bytes against a 64-byte line means three elements fit per line in one configuration (offsets 0/24/48) but the next three straddle (72/96/120 -> lines starting at 64 and 128). Across a wave of 32 lanes reading consecutive elements at stride 24, every wave reads from at least 12 distinct cache lines instead of the 12 minimum for stride-32 (which packs cleanly into 64-byte lines).

The cost surfaces as cache thrashing. Each extra line touched is an extra tag check, an extra MSHR slot, and an extra eviction candidate competing with the rest of the working set. On RDNA 3, the L1 has 16 KB per CU and an L0 of 16 KB per WGP — small enough that a streaming pass over a few hundred kilobytes of structured data evicts itself rapidly when stride alignment is poor. NVIDIA Ada's larger 128 KB L1 absorbs more of the cost but still serialises the extra tag lookups in the load pipeline.

Stride 24 is the canonical offender: a `struct { float3 pos; float pad_or_attr; }` is 16 bytes (clean), but `struct { float3 pos; float2 uv; }` is 20 bytes packed plus 4 bytes implicit pad to vec-alignment = 24 bytes. The packing is correct, the reflection-reported stride is honest, and the cache cost is silent. The rule pulls the silent cost forward to lint time so the author can pad to 32 bytes (one extra `float`), reorder to bring the structure to a power-of-two stride, or split the structure into parallel `StructuredBuffer<float3> Positions` + `StructuredBuffer<float2> UVs` for SoA streaming.

## Examples

### Bad

```hlsl
struct Vertex {
    float3 position;   // 12 bytes
    float2 uv;         // 8 bytes
    // Reflected stride: 24 bytes (packed). 24 is a multiple of 4 but not
    // of 32; consecutive elements straddle 64-byte cache lines on RDNA.
};
StructuredBuffer<Vertex> Vertices : register(t0);
```

### Good

```hlsl
struct Vertex {
    float3 position;   // 12 bytes
    float  tangent_w;  // 4  bytes
    float2 uv;         // 8  bytes
    float2 uv2;        // 8  bytes
    // Reflected stride: 32 bytes — exactly two elements per RDNA 64-byte
    // line, exactly four per Ada 128-byte line.
};
StructuredBuffer<Vertex> Vertices : register(t0);

// Or split into parallel SoA streams when the consumer reads only some fields.
StructuredBuffer<float3> Positions : register(t0);
StructuredBuffer<float2> UVs       : register(t1);
```

## Options

- `cache-line-target` (integer, default: `32`) — the cache-line alignment to check against, in bytes. Valid values: `16`, `32`, `64`, `128`. Set to `64` to match AMD RDNA 2/3 vector L1 line size, or `128` to match NVIDIA Turing/Ada L1. The rule fires when `stride % cache-line-target != 0` and `stride > 4`. Configure per-project:

```toml
[rules.structured-buffer-stride-not-cache-aligned]
cache-line-target = 64
```

## Fix availability

**suggestion** — Padding or reordering struct members has CPU-side layout implications (SoA splits especially). The diagnostic reports the observed stride and the next valid stride; the author chooses among padding, reordering, or SoA splitting.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — same packing-rules surface for constant buffers
- Related rule: [byteaddressbuffer-load-misaligned](byteaddressbuffer-load-misaligned.md) — alignment hazard on the raw-byte path
- Related rule: [structured-buffer-stride-mismatch](structured-buffer-stride-mismatch.md) — HLSL packing-rule mismatch with declared stride
- HLSL reference: structured buffer layout rules in the DirectX HLSL Shader Model 6.x documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/structured-buffer-stride-not-cache-aligned.md)

*© 2026 NelCit, CC-BY-4.0.*
