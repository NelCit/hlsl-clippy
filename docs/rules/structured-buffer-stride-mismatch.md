---
id: structured-buffer-stride-mismatch
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# structured-buffer-stride-mismatch

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `StructuredBuffer<T>` or `RWStructuredBuffer<T>` declaration where the element type `T` has a byte size that is not a multiple of 16. The rule uses Slang's reflection API to determine `sizeof(T)` after HLSL struct-packing rules are applied, then checks `sizeof(T) % 16 != 0`. Common triggers: `StructuredBuffer<Particle>` where `Particle` contains only `float3 pos` (12 bytes, remainder 12); `StructuredBuffer<GpuLight>` where `GpuLight` has `float3 + float + float3 = 28 bytes`, remainder 12. See `tests/fixtures/phase3/bindings.hlsl`, lines 42–45 (`Particle`, 12 bytes) and `tests/fixtures/phase3/bindings_extra.hlsl`, lines 59–67 (`GpuLight`, 28 bytes).

## Why it matters on a GPU

`StructuredBuffer<T>` elements are accessed through the texture / L2 / L1 cache hierarchy, which operates on cache-line granularities of 64 or 128 bytes depending on the architecture. For efficient cache utilisation, element strides should align to 16-byte boundaries so that element boundaries coincide with the natural sub-cacheline boundaries used by gather/scatter units on RDNA and Turing hardware.

When the element stride is not 16-aligned, the D3D12 runtime and most drivers round the stride up to the next multiple of 4 bytes (the minimum `StructuredBuffer` stride requirement) but do not pad elements in the GPU buffer itself. The result is that consecutive element loads may straddle cache lines in a pattern that maximises cache-line waste. For a 12-byte `float3` struct, each element load reads 12 bytes but the effective stride used by some driver path rounds to 12, meaning that every four elements straddle a 48-byte region that could have been three 16-byte aligned loads. On RDNA the scatter/gather unit operates on 16-byte-aligned 128-bit words; a 12-byte stride means every third element load spans two 16-byte words, requiring an extra memory transaction compared to a 16-byte aligned element.

The fix is to pad the struct to the next 16-byte multiple: add a `float _pad` member to bring `float3` to `float4` (16 bytes) or add a `float _pad` to bring 28-byte `GpuLight` to 32 bytes. The wasted 4 bytes per element are a 25% size overhead for a 12-byte struct, which may seem costly, but the alternative is a 25% bandwidth overhead on every element load due to partial cache-line utilisation. For large structured buffers accessed at high frequency, the aligned form is consistently faster on all current GPU architectures.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 42-45
// HIT(structured-buffer-stride-mismatch): Particle is 12 bytes;
// not 16-aligned — 25% bandwidth waste on every element load.
struct Particle {
    float3 pos;  // 12 bytes, not padded to 16
};
StructuredBuffer<Particle> Particles : register(t0);

// From tests/fixtures/phase3/bindings_extra.hlsl, lines 59-67
// HIT(structured-buffer-stride-mismatch): GpuLight is 28 bytes — not 16-aligned.
struct GpuLight {
    float3 position;   // 12 bytes
    float  radius;     //  4 bytes
    float3 color;      // 12 bytes
                       // total: 28 bytes — not 16-aligned
};
StructuredBuffer<GpuLight> LightBuffer : register(t4);
```

### Good

```hlsl
// Pad Particle to 16 bytes.
struct Particle {
    float3 pos;   // 12 bytes
    float  _pad;  //  4 bytes — explicit padding to 16 bytes total
};
StructuredBuffer<Particle> Particles : register(t0);

// Pad GpuLight to 32 bytes.
struct GpuLight {
    float3 position;  // 12 bytes
    float  radius;    //  4 bytes
    float3 color;     // 12 bytes
    float  _pad;      //  4 bytes — total 32 bytes (2 × 16-byte aligned)
};
StructuredBuffer<GpuLight> LightBuffer : register(t4);
```

## Options

none

## Fix availability

**suggestion** — Adding explicit padding members changes the CPU-side struct layout. The `sizeof` of the C++ mirror struct must be updated to match, and any code that fills the buffer via `memcpy` or manually computes element offsets must be verified. `hlsl-clippy fix` suggests the padded struct as a comment but does not insert padding members automatically.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — alignment gaps in cbuffer layouts
- Related rule: [rwresource-read-only-usage](rwresource-read-only-usage.md) — RW resource only ever read — should be demoted to SRV
- HLSL `StructuredBuffer` stride requirements in the DirectX API reference
- D3D12 `D3D12_BUFFER_SRV` `StructureByteStride` field documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/structured-buffer-stride-mismatch.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
