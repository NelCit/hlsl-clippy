---
id: cbuffer-divergent-index
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# cbuffer-divergent-index

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A read from a `cbuffer`, `ConstantBuffer<T>`, or inline constant buffer (ICB) where the field is selected through an index that is per-lane divergent — for example, `cb.array[lane_idx]` where `lane_idx` is derived from a semantic input (`SV_InstanceID`, `TEXCOORD`, or similar) or from a wave-divergent computation. The rule relies on Slang's uniformity analysis to determine whether the index is wave-uniform or potentially divergent. It does not fire on compile-time-constant indices or on indices that Slang can prove are uniform across all lanes in the wave.

Note: Phase 3 detects the syntactic pattern via Slang reflection; the full uniformity-propagation analysis described in the ROADMAP under Phase 4 (`cbuffer-divergent-index`) will catch more complex derived-index cases. The Phase 3 version is conservative and fires only on clearly divergent indices (semantic inputs, function parameters without uniform qualification).

## Why it matters on a GPU

cbuffer and constant-buffer data is served to shader threads through a dedicated constant-data path that is optimised for wave-uniform access. On AMD RDNA the constant data arrives via the scalar data cache (K-cache) and the scalar register file (SGPRs): all lanes in the wave share a single fetch, which is the correct and fast path when all lanes read the same index. On NVIDIA hardware, NVIDIA's developer documentation explicitly identifies divergent constant buffer reads as a serialization hazard: when lanes in a warp read different indices into a constant buffer array, the hardware cannot serve them as a single broadcast. Instead, the constant cache performs the reads sequentially, one unique index at a time, turning a single-cycle broadcast into a serialized sequence of up to 32 (or 64 on Turing with 2x warp scheduling) individual constant loads.

The L1 constant cache becomes the bottleneck in this scenario. Each divergent index may evict a prior entry from the constant cache, causing subsequent loads of other cbuffer fields to miss. In a pixel shader that indexes a material-property array in a constant buffer by per-pixel material ID, this pattern can reduce effective constant-cache throughput by an order of magnitude compared to a wave-uniform index. The correct fix is to move the indexed data to a `StructuredBuffer<T>` or `Buffer<T>` (which is served through the texture/L2 cache path, designed for per-lane divergent reads) and keep the constant buffer for wave-uniform data only.

## Examples

### Bad

```hlsl
// cbuffer array indexed by a per-pixel divergent value.
cbuffer MaterialCB : register(b0) {
    float4 MaterialColors[64];  // indexed per pixel — divergent
};

float4 ps_material(float4 pos : SV_Position,
                   uint matId : TEXCOORD1) : SV_Target {
    // HIT(cbuffer-divergent-index): matId is per-pixel divergent;
    // NVIDIA serializes constant-cache loads on this pattern.
    return MaterialColors[matId];
}
```

### Good

```hlsl
// Move the indexed array to a StructuredBuffer — the texture/L2 cache path
// handles per-lane divergent reads efficiently.
StructuredBuffer<float4> MaterialColors : register(t0);

float4 ps_material(float4 pos : SV_Position,
                   uint matId : TEXCOORD1) : SV_Target {
    return MaterialColors[matId];  // SHOULD-NOT-HIT(cbuffer-divergent-index)
}

// Wave-uniform cbuffer access is fine and stays fast.
cbuffer FrameCB : register(b0) {
    float4x4 ViewProj;  // same for all lanes — no hit
};
```

## Options

none

## Fix availability

**suggestion** — Moving indexed data from a `cbuffer` to a `StructuredBuffer` requires changes to both the HLSL resource declarations and the D3D12 root signature / pipeline state setup on the CPU side. `shader-clippy fix` generates a suggested `StructuredBuffer` declaration as a comment but does not restructure the cbuffer automatically.

## See also

- Related rule: [oversized-cbuffer](oversized-cbuffer.md) — cbuffer exceeds the default 4 KB threshold
- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — alignment gaps in cbuffer layouts
- NVIDIA developer documentation: "Divergent Constant Buffer Accesses" (Turing architecture optimization guide)
- HLSL `StructuredBuffer` vs `ConstantBuffer` access pattern documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/cbuffer-divergent-index.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
