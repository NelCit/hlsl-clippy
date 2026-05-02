---
id: texture-array-known-slice-uniform
category: texture
severity: note
applicability: suggestion
since-version: "v0.3.0"
phase: 3
---

# texture-array-known-slice-uniform

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

Calls to `Texture2DArray.Sample(sampler, float3(uv, K))` or `Texture2DArray.SampleLevel(sampler, float3(uv, K), lod)` where the slice coordinate `K` (the z component of the UV argument) is statically determinable as dynamically uniform for the entire draw call or dispatch — specifically, when `K` is a `cbuffer` field, a root constant, or a literal value. The rule uses Slang reflection to confirm that the slice source is a constant buffer member, and then checks whether the array resource carries more than one slice. It does not fire when `K` is a per-thread value such as a UAV index, a `SV_InstanceID`-derived expression, or a groupshared variable.

## Why it matters on a GPU

A `Texture2DArray` resource descriptor carries an extra dimension in its hardware surface state: the array layer count. On AMD RDNA, NVIDIA Turing, and Intel Xe-HPG, the TMU's address-generation unit must resolve a 3D coordinate `(x, y, layer)` rather than a 2D coordinate `(x, y)` for every fetch. When the layer index is a constant across the entire dispatch, every thread in every wave loads the same layer, and the array dimension adds no useful variation — the shader is accessing a single fixed 2D slice of the array. The TMU still processes the full 3D address, however, and the resource descriptor still reserves VRAM for all array layers even if only one is ever read.

When the layer is uniform across all waves, binding a plain `Texture2D` instead of a `Texture2DArray` allows the TMU to take the simpler 2D address path. On RDNA 3, 2D texture addressing avoids one multiply in the address-generation unit per sample, a saving that is small per invocation but adds up in high-sample-rate passes such as deferred lighting or ambient occlusion. More practically, replacing the array resource with a single-layer texture reduces the VRAM footprint of the binding to exactly one layer, which is important when the array holds large irradiance maps or BRDF LUT slices that must fit within a tier-1 descriptor-heap allocation.

The rule is a `suggestion` rather than `machine-applicable` because acting on it requires a root-signature change: the CPU side must bind a `Texture2D` SRV rather than a `Texture2DArray` SRV at the relevant slot. This is a non-trivial API change. An alternative fix that preserves the array type is to use `NonUniformResourceIndex` or to confirm that the layer index is truly wave-uniform, which suppresses the diagnostic. Shader permutations where the same HLSL is compiled with different dispatch-level slice values may need a `cbuffer` field rather than a hard-coded literal.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/textures.hlsl, line 32
// HIT(texture-array-known-slice-uniform): slice index is dynamically uniform.
Texture2DArray Stack     : register(t2);
SamplerState   Bilinear  : register(s0);

cbuffer Globals : register(b0) {
    float2 InvScreen;
    uint   StackSlice;   // dynamically uniform across the dispatch
};

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 stacked = Stack.Sample(Bilinear, float3(uv, StackSlice)).rgb;
    // ...
}

// From tests/fixtures/phase3/textures_extra.hlsl, lines 50-54
// HIT(texture-array-known-slice-uniform): ArraySlice is a cbuffer value —
// dynamically uniform; could demote to Texture2D.
float3 irradiance_probe(float2 uv) {
    return IrradianceArray.Sample(LinearWrap, float3(uv, (float)ArraySlice)).rgb;
}
```

### Good

```hlsl
// Demote to Texture2D if only one slice is ever needed per dispatch:
Texture2D    StackSlice0 : register(t2);
SamplerState Bilinear    : register(s0);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 stacked = StackSlice0.Sample(Bilinear, uv).rgb;
    // ...
}

// When the array must remain (multiple slices across permutations), keep the
// array but suppress the diagnostic at the call site:
// hlsl-clippy: allow(texture-array-known-slice-uniform)
float3 stacked = Stack.Sample(Bilinear, float3(uv, StackSlice)).rgb;

// Per-thread divergent slice — rule does not fire:
// SHOULD-NOT-HIT(texture-array-known-slice-uniform)
float3 irradiance_per_thread(float2 uv, uint threadSlice) {
    return IrradianceArray.Sample(LinearWrap, float3(uv, (float)threadSlice)).rgb;
}
```

## Options

none

## Fix availability

**suggestion** — The rule can propose demoting the resource declaration from `Texture2DArray` to `Texture2D` and removing the z component from the UV argument. Because the fix requires a corresponding CPU-side binding change (creating a `D3D12_SRV_DIMENSION_TEXTURE2D` view instead of `D3D12_SRV_DIMENSION_TEXTURE2DARRAY`), `hlsl-clippy fix` shows the candidate HLSL edits but does not apply them automatically.

## See also

- Related rule: [`samplelevel-with-zero-on-mipped-tex`](samplelevel-with-zero-on-mipped-tex.md) — similar uniform-index pattern for mip levels
- Related rule: [`texture-as-buffer`](texture-as-buffer.md) — related dimensionality-reduction suggestion
- HLSL intrinsic reference: `Texture2DArray.Sample`, `Texture2D.Sample` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/texture-array-known-slice-uniform.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
