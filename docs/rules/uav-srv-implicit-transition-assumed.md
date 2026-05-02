---
id: uav-srv-implicit-transition-assumed
category: bindings
severity: warn
applicability: none
since-version: v0.5.0
phase: 3
---

# uav-srv-implicit-transition-assumed

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A shader that writes to a UAV `U` and subsequently reads from an SRV `S`, where Slang reflection identifies `U` and `S` as views over the same underlying resource (or aliased GPU virtual address range). The detector enumerates UAV and SRV bindings via reflection, cross-references their backing resource identity (binding metadata, descriptor heap offsets where reflected, or explicit alias annotations), and fires when at least one such pair appears in the reflected binding set with a write-then-read sequence in the AST. **D3D12-relevant:** Vulkan handles the equivalent through `VkImageMemoryBarrier` and explicit pipeline-stage masks at the API level; Metal handles it through `MTLResourceUsage` and command-encoder dependencies; this rule still surfaces a portability concern because every backend requires *some* form of explicit synchronisation between writer and reader and the lint flags the assumption that the runtime will paper over it.

## Why it matters on a GPU

D3D12 makes resource state transitions explicit. A resource bound as a UAV in one draw / dispatch and then read as an SRV in the next requires an explicit `D3D12_RESOURCE_BARRIER` (or the new enhanced barrier `D3D12_TEXTURE_BARRIER` on D3D12 Agility SDK 1.7+) between the two — the transition flushes the UAV writer's L1 / L0 caches, invalidates the reader's L1, and on AMD RDNA 2/3 specifically issues a wait for the writer's shader-engine to drain before the reader's wave can launch. Without the barrier, the reader sees stale data from before the write (cached in its own L1), partial data (writer not yet drained), or data that lands during the read (race condition). The hardware does not detect the hazard; the runtime does not insert the barrier; the application is responsible for issuing it.

Detecting the missing barrier requires API-level analysis the linter does not have — the barrier is issued by C++ code, not HLSL. What the linter *can* do is surface the alias from reflection: when the same resource is bound as both UAV `U` and SRV `S` and the shader reads `S` after writing `U`, the application is implicitly assuming a barrier exists between the two. The rule documents the assumption so the developer can audit the application-side barrier logic rather than discover the missing barrier through GPU validation layer warnings or, worse, through a bug report from a customer whose driver sequences the work differently.

The rule is documentation-grade: it does not propose a code change (the resolution is on the C++ side) and does not propose suppressing the alias (some pipelines deliberately ping-pong the same resource). It surfaces the cross-binding alias so it is not invisible.

## Examples

### Bad

```hlsl
// Reflection identifies WriteOut and ReadIn as views over the same resource.
RWTexture2D<float4> WriteOut : register(u0);
Texture2D<float4>   ReadIn   : register(t0);

[numthreads(8, 8, 1)]
void cs_pingpong(uint3 dtid : SV_DispatchThreadID) {
    WriteOut[dtid.xy] = float4(1, 0, 0, 1);
    // Reading the SRV after writing the UAV against the same resource
    // requires an explicit barrier on the application side.
    float4 v = ReadIn.Load(int3(dtid.xy, 0));
    WriteOut[dtid.xy] = v + float4(0, 1, 0, 0);
}
```

### Good

```hlsl
// Distinct resources backing UAV and SRV — no aliasing, no implicit
// transition assumption.
RWTexture2D<float4> WriteOut : register(u0);
Texture2D<float4>   ReadIn   : register(t1);  // different resource

[numthreads(8, 8, 1)]
void cs_distinct(uint3 dtid : SV_DispatchThreadID) {
    float4 v = ReadIn.Load(int3(dtid.xy, 0));
    WriteOut[dtid.xy] = v + float4(0, 1, 0, 0);
}

// Or, if ping-ponging is intentional, document the application-side
// barrier and suppress the rule for this scope:
//   // hlsl-clippy disable-next-line uav-srv-implicit-transition-assumed
```

## Options

none

## Fix availability

**none** — The remediation is on the application / C++ side (issuing the right `D3D12_RESOURCE_BARRIER` or Vulkan / Metal equivalent). The linter cannot verify the barrier is issued and does not propose a shader-side change. The rule exists to surface the alias so the developer can audit the barrier logic.

## See also

- Related rule: [cbuffer-large-fits-rootcbv-not-table](cbuffer-large-fits-rootcbv-not-table.md) — D3D12 binding ergonomics in the same category
- Related rule: [static-sampler-when-dynamic-used](static-sampler-when-dynamic-used.md) — D3D12 root-signature hygiene
- D3D12 reference: `D3D12_RESOURCE_BARRIER`, enhanced barriers in the D3D12 Agility SDK documentation
- Vulkan reference: `VkImageMemoryBarrier`, `VkPipelineStageFlags` in the Vulkan synchronization documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/uav-srv-implicit-transition-assumed.md)

*© 2026 NelCit, CC-BY-4.0.*
