---
id: non-uniform-resource-index
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# non-uniform-resource-index

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A dynamic index into a resource array parameter — such as `Texture2D textures[]`, `ConstantBuffer<T> cbs[]`, or any other unbounded / bounded resource array — where the index is a per-lane divergent value and is not wrapped in `NonUniformResourceIndex(...)`. The rule uses Slang's reflection API to identify parameters of array-of-resource type, then uses Slang's uniformity analysis to determine whether each index expression could differ across lanes in the same wave. It does not fire when the index is a compile-time constant, a root constant, or any value that Slang can prove is wave-uniform.

## Why it matters on a GPU

The DXIL specification (and by extension the HLSL specification for SM 5.1 and later) defines it as undefined behaviour to index a resource array with a non-uniform value without the `NonUniformResourceIndex` marker. The reason is architectural: resource descriptors are resolved by the driver before shader dispatch, and the hardware uses a single descriptor index per wave to look up the resource binding. If all lanes in a wave use the same index (uniform access), the driver emits a single descriptor load and broadcasts the resource handle to all lanes. If the index varies per lane (non-uniform), the driver must emit a waterfall loop — a sequential iteration over unique index values, masking inactive lanes at each step.

Without `NonUniformResourceIndex`, the driver has no signal that the index is non-uniform and may silently use the value from lane 0 for the entire wave. On AMD RDNA hardware this produces a shader where all lanes see the resource bound to lane 0's index, discarding the correct resource for all other lanes. On NVIDIA Turing the outcome is similarly hardware-dependent and undefined. In both cases the failure manifests as rendering artefacts (wrong textures, garbage data) that are invisible to the GPU validation layer in release mode and very difficult to reproduce reliably.

When `NonUniformResourceIndex` is applied, the driver emits the correct (waterfall) loop. This is slower than a uniform access — up to 64x in the worst case on a 64-wide wave — but it is correct. If the index is known to be uniform at a specific call site, the marker can be omitted at that site; the rule fires only when divergence cannot be ruled out.

## Examples

### Bad

```hlsl
// Texture array indexed by a per-pixel divergent value — missing marker.
Texture2D<float4> g_textures[] : register(t0, space1);
SamplerState      g_sampler  : register(s0);

float4 ps_main(float2 uv : TEXCOORD0, uint matId : TEXCOORD1) : SV_Target {
    // HIT(non-uniform-resource-index): matId is per-pixel divergent;
    // spec requires NonUniformResourceIndex.
    return g_textures[matId].Sample(g_sampler, uv);
}
```

### Good

```hlsl
Texture2D<float4> g_textures[] : register(t0, space1);
SamplerState      g_sampler  : register(s0);

float4 ps_main(float2 uv : TEXCOORD0, uint matId : TEXCOORD1) : SV_Target {
    // SHOULD-NOT-HIT(non-uniform-resource-index): correctly marked.
    return g_textures[NonUniformResourceIndex(matId)].Sample(g_sampler, uv);
}

// If matId is pushed as a root constant (wave-uniform), no marker is needed:
cbuffer DrawCB : register(b0) {
    uint g_matId;  // same for all invocations in this draw call
};

float4 ps_uniform(float2 uv : TEXCOORD0) : SV_Target {
    return g_textures[g_matId].Sample(g_sampler, uv);  // no HIT — uniform index
}
```

## Options

none

## Fix availability

**suggestion** — `--fix` wraps the captured index expression in `NonUniformResourceIndex(...)`. The wrap evaluates the index exactly once (no duplication), so it is side-effect-safe even when the index is a non-trivial expression. The fix is marked `machine_applicable = false` because the rule cannot prove the index is actually divergent at every call site: applying the marker on a known-uniform index is harmless on most drivers but may impose a small waterfall overhead where none is needed. Verify call-site uniformity before accepting the fix in bulk.

## See also

- Related rule: [descriptor-heap-no-non-uniform-marker](descriptor-heap-no-non-uniform-marker.md) — SM 6.6 `ResourceDescriptorHeap[i]` without `NonUniformResourceIndex`
- Related rule: [descriptor-heap-type-confusion](descriptor-heap-type-confusion.md) — sampler fetched from the wrong heap type
- HLSL SM 5.1 specification: `NonUniformResourceIndex` intrinsic
- DXIL specification: non-uniform resource index requirements
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/non-uniform-resource-index.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
