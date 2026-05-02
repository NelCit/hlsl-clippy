---
id: rov-without-earlydepthstencil
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# rov-without-earlydepthstencil

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A pixel shader entry point that declares one or more `RasterizerOrderedBuffer<T>`, `RasterizerOrderedTexture2D<T>`, or any other `RasterizerOrdered*` resource (ROV) without the `[earlydepthstencil]` function attribute, and without a `discard` statement or a `SV_Depth` write that would make early depth legally ambiguous. The rule uses Slang's reflection API to identify entry points with ROV-typed resource bindings and checks whether the `[earlydepthstencil]` attribute is present. It does not fire when the shader contains `discard`, writes `SV_Depth`, or writes `SV_Coverage` — situations where `[earlydepthstencil]` would change semantics.

## Why it matters on a GPU

ROVs enforce ordering between pixel shader invocations that cover the same pixel: later-rasterized primitives must not commit their ROV writes until earlier-rasterized primitives have completed. On AMD RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace, this ordering is implemented via a serialization primitive — typically a per-pixel lock or a per-pixel write-order fence — that the hardware acquires before any ROV access and releases after. The critical section is the span of shader code between the lock acquisition and the lock release.

Without `[earlydepthstencil]`, the D3D12 specification allows the depth test to be performed after the shader runs (late depth). In late-depth mode, the ROV lock must be held across the entire shader body — including all code that runs before the first ROV access — because the hardware cannot know at dispatch time whether this pixel will pass the depth test. On AMD GCN / RDNA hardware, this means the per-pixel lock is acquired at the start of the pixel shader and held until the pixel either returns or is discarded, even if the ROV write occurs near the end. On NVIDIA, the ordering fence spans the full shader execution.

With `[earlydepthstencil]`, the depth test runs before the pixel shader body. Pixels that fail depth are culled before they enter the shader, never acquiring the ROV lock. The ROV ordering critical section begins at the first ROV access and ends at the last, which may be a much smaller portion of the shader body than the full shader. This reduces the average critical-section length, reduces contention between waves competing for the per-pixel lock, and can increase effective parallelism when many waves render non-overlapping geometry. NVIDIA's ROV documentation recommends `[earlydepthstencil]` as a prerequisite for acceptable ROV performance in all but the simplest single-primitive scenes.

## Examples

### Bad

```hlsl
// ROV declared without [earlydepthstencil] — critical section spans full shader.
RasterizerOrderedTexture2D<float4> g_rov : register(u0);

// HIT(rov-without-earlydepthstencil): no [earlydepthstencil] attribute.
float4 ps_rov(float4 pos : SV_Position,
              float2 uv  : TEXCOORD0) : SV_Target {
    // Expensive work before the ROV access — all inside the critical section.
    float4 base = some_texture.Sample(some_sampler, uv);
    float4 prev = g_rov[uint2(pos.xy)];
    g_rov[uint2(pos.xy)] = base + prev * 0.5;
    return base;
}
```

### Good

```hlsl
RasterizerOrderedTexture2D<float4> g_rov : register(u0);

// [earlydepthstencil] moves depth test before shader body;
// pixels that fail depth never enter the ROV critical section.
[earlydepthstencil]
float4 ps_rov(float4 pos : SV_Position,
              float2 uv  : TEXCOORD0) : SV_Target {
    float4 base = some_texture.Sample(some_sampler, uv);
    float4 prev = g_rov[uint2(pos.xy)];
    g_rov[uint2(pos.xy)] = base + prev * 0.5;
    return base;
}
```

## Options

none

## Fix availability

**suggestion** — Adding `[earlydepthstencil]` changes observable behaviour when the shader contains `discard`, writes `SV_Depth`, or writes `SV_Coverage` — in those cases the rule does not fire. When the rule fires (no depth-modifying operations present), adding the attribute is safe, but `hlsl-clippy fix` requires explicit author confirmation before inserting a shader-attribute that affects rasterisation semantics.

## See also

- Related rule: [all-resources-bound-not-set](all-resources-bound-not-set.md) — project-level flag that enables additional driver optimisations
- HLSL `[earlydepthstencil]` attribute documentation in the DirectX HLSL reference
- D3D12 specification: Rasterizer-Ordered Views — ordering semantics and depth-test interaction
- NVIDIA developer documentation: ROV performance guidelines and `EarlyDepthStencil` interaction
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/rov-without-earlydepthstencil.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
