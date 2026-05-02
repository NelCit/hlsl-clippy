---
id: all-resources-bound-not-set
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# all-resources-bound-not-set

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A project-level configuration where shaders are compiled without the `-all-resources-bound` flag (or the equivalent `D3DCOMPILE_ALL_RESOURCES_BOUND` flag in legacy compilation pipelines) despite the project's root signature declaring a fully populated descriptor table — one where all declared descriptor ranges are unconditionally bound for every draw or dispatch in the pipeline. The rule operates at project/pipeline level rather than per-shader: it uses Slang's compilation flag introspection to check whether `AllResourcesBound` is set, and cross-references this against the shader's resource declarations to determine whether all declared resources are statically reachable and would be bound in a complete root signature. It fires as a project-level suggestion when the flag is absent and the shader's resource layout is consistent with full population.

## Why it matters on a GPU

The `-all-resources-bound` compile flag communicates to the driver that the application guarantees every resource declared in the root signature is bound before any draw or dispatch call. This guarantee unlocks a class of driver-side optimisations that are otherwise disabled for correctness reasons in the general case.

NVIDIA documents this flag explicitly in their D3D12 performance guide: when `-all-resources-bound` is set, the driver can defer residency checks from shader dispatch time to pipeline state object (PSO) creation time. Without the flag, the driver performs per-draw residency validation — checking that each bound resource's backing memory is GPU-resident — as part of command-list execution. With the flag, the driver batches residency validation at PSO creation and relies on the application's guarantee to skip the per-draw check. On NVIDIA Turing this removes a serialization point in the command-processor that otherwise stalls between draw calls while residency is verified. In draw-call-heavy scenes (e.g., forward-plus lighting with hundreds of material draw calls) the aggregate savings can be measurable.

On AMD RDNA hardware, the flag similarly allows the driver to omit per-draw descriptor validation and to emit a leaner command packet sequence. The AMD performance guide for D3D12 lists resource-binding completeness guarantees as one of the conditions for the "optimal fast path" through the command processor. Beyond residency checks, the flag also enables certain instruction-scheduling optimisations in the shader compiler: knowing that a resource declared in the root signature is always present allows the compiler to omit null-descriptor guard branches that would otherwise be emitted for robustness.

The flag is safe to set when the application always binds all declared resources before any draw or dispatch. Most production renderers with a well-defined material system and a fixed root signature satisfy this condition. The diagnostic is a project-level suggestion, not a per-file error, because the decision to set the flag belongs to the build system and compilation pipeline rather than to any individual shader file.

## Examples

### Bad

```hlsl
// Compiled without -all-resources-bound. Root signature fully populates
// all descriptor ranges but the flag is absent — driver cannot skip
// per-draw residency checks.

// Build system invocation:
//   dxc -T ps_6_6 shader.hlsl -Fo shader.dxo
//   (missing: -all-resources-bound)

Texture2D<float4>    g_albedo   : register(t0);
Texture2D<float4>    g_normal   : register(t1);
SamplerState         g_sampler  : register(s0);
cbuffer FrameCB      : register(b0) { float4x4 ViewProj; };

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float4 albedo = g_albedo.Sample(g_sampler, uv);
    float4 normal = g_normal.Sample(g_sampler, uv);
    return albedo * normal;
}
```

### Good

```hlsl
// Same shader, compiled with -all-resources-bound.
// Build system invocation:
//   dxc -T ps_6_6 shader.hlsl -Fo shader.dxo -all-resources-bound

// Slang equivalent: SlangCompileRequest flag AllResourcesBound set to true.

Texture2D<float4>    g_albedo   : register(t0);
Texture2D<float4>    g_normal   : register(t1);
SamplerState         g_sampler  : register(s0);
cbuffer FrameCB      : register(b0) { float4x4 ViewProj; };

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float4 albedo = g_albedo.Sample(g_sampler, uv);
    float4 normal = g_normal.Sample(g_sampler, uv);
    return albedo * normal;
}
```

## Options

none

## Fix availability

**suggestion** — Adding `-all-resources-bound` to the compilation invocation is a single-flag change, but the application must actually guarantee that all declared resources are bound on every draw and dispatch. `hlsl-clippy fix` does not modify build scripts; it generates a diagnostic message naming the missing flag and the Slang API equivalent, leaving the author to verify the binding completeness guarantee before enabling the flag.

## See also

- Related rule: [rov-without-earlydepthstencil](rov-without-earlydepthstencil.md) — ROV resource in PS without `[earlydepthstencil]`
- Related rule: [unused-cbuffer-field](unused-cbuffer-field.md) — unused fields that may cause a resource to appear partially populated
- NVIDIA D3D12 performance guide: `-all-resources-bound` and deferred residency checks
- AMD D3D12 performance guide: resource-binding completeness and the command-processor fast path
- DXC documentation: `-all-resources-bound` compile flag
- Slang API: `ICompileRequest` — `AllResourcesBound` session flag
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/all-resources-bound-not-set.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
