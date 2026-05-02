---
id: rwresource-read-only-usage
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# rwresource-read-only-usage

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

An `RWBuffer<T>`, `RWStructuredBuffer<T>`, `RWTexture1D<T>`, `RWTexture2D<T>`, `RWTexture3D<T>`, or any other read-write (UAV) resource declaration that is only ever read in the shader — never written to and never passed to an intrinsic that performs a write (e.g., `InterlockedAdd`, `Store`, assignment via `[]` operator). The rule uses Slang's reflection API to identify UAV-typed resources and checks all access sites for write operations. See `tests/fixtures/phase3/bindings.hlsl`, lines 48–53 (`ReadOnlyRW`, accessed only as `ReadOnlyRW[0]` on the right-hand side) and `tests/fixtures/phase3/bindings_extra.hlsl`, lines 50–55 (`AccumBuffer`, accessed only via `AccumBuffer.Load(...)`).

## Why it matters on a GPU

UAV (unordered access view) resources and SRV (shader resource view) resources differ in how the GPU's memory subsystem handles them. An SRV is read-only at the hardware level: the GPU can apply aggressive caching, prefetching, and compression (e.g., delta colour compression on texture SRVs on AMD RDNA) because the hardware knows no write will invalidate cached data during the current dispatch. A UAV is read-write: the GPU must treat each access as potentially invalidating the cache, which disables or limits these optimisations.

Declaring a resource as UAV when it is only read prevents the driver from applying read-only optimisations. On AMD RDNA, texture compression (DCC — Delta Colour Compression) is applied automatically to SRV-bound textures but must be flushed and disabled when a resource transitions to UAV state. A resource that is bound as UAV throughout its lifetime, even though it is only read, forces the driver to assume it may be written and suppresses compression. On NVIDIA, UAV accesses go through a different cache coherency path than SRV accesses; read-only UAV access pays the coherency overhead unnecessarily.

Beyond performance, a resource declared as `RW*` signals to the CPU-side code and to D3D12 validation that writes are expected. This increases the resource's state-tracking burden (barrier requirements) and may prevent the runtime from placing the resource in a more efficient memory layout. Demoting to an SRV removes the write path entirely, allowing the runtime and driver to apply the full set of read-only optimisations.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 47-53
// HIT(rwresource-read-only-usage): ReadOnlyRW is only ever indexed for reading.
RWStructuredBuffer<uint> ReadOnlyRW : register(u0);

float4 entry_main(float4 pos : SV_Position) : SV_Target {
    uint val = ReadOnlyRW[0];  // read-only access — no writes anywhere
    return float4(val, 0, 0, 1);
}

// From tests/fixtures/phase3/bindings_extra.hlsl, lines 50-55
// HIT(rwresource-read-only-usage): AccumBuffer only loaded, never stored.
RWTexture2D<float4> AccumBuffer : register(u1);

float4 ps_read_only_rw(float4 pos : SV_Position) : SV_Target {
    uint2 coord = (uint2)pos.xy;
    float4 val = AccumBuffer.Load(int3(coord, 0));  // read-only Load
    return val;
}
```

### Good

```hlsl
// Demote to SRV — enables read-only optimisations and compression.
StructuredBuffer<uint> ReadOnlyData : register(t0);

float4 entry_main(float4 pos : SV_Position) : SV_Target {
    uint val = ReadOnlyData[0];
    return float4(val, 0, 0, 1);
}

Texture2D<float4> AccumBuffer : register(t1);

float4 ps_read_only_rw(float4 pos : SV_Position) : SV_Target {
    uint2 coord = (uint2)pos.xy;
    float4 val = AccumBuffer.Load(int3(coord, 0));
    return val;
}
```

## Options

none

## Fix availability

**suggestion** — Demoting a UAV to an SRV changes the resource's bind point from a `u` register to a `t` register and requires updating the D3D12 root signature, the pipeline state object descriptor table, and any resource-barrier code on the CPU side. The HLSL change (renaming the resource type and register) is mechanical, but the surrounding CPU-side changes must be made in concert. `hlsl-clippy fix` generates the suggested HLSL declaration change as a comment but does not apply it automatically.

## See also

- Related rule: [structured-buffer-stride-mismatch](structured-buffer-stride-mismatch.md) — element stride not 16-aligned for StructuredBuffer
- Related rule: [dead-store-sv-target](dead-store-sv-target.md) — `SV_Target` written and immediately overwritten
- D3D12 resource state documentation: `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` vs `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`
- AMD RDNA DCC (Delta Colour Compression) — requires SRV-only usage to stay active
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/rwresource-read-only-usage.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
