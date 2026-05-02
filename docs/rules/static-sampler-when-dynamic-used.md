---
id: static-sampler-when-dynamic-used
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# static-sampler-when-dynamic-used

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `SamplerState` declared as a dynamic sampler binding (i.e. one that consumes a sampler descriptor slot through a descriptor table or root-descriptor entry) whose state — `Filter`, `AddressU/V/W`, `MaxAnisotropy`, `BorderColor`, `MipLODBias`, `MinLOD`, `MaxLOD` — never varies across draws or dispatches in any reflection-visible call site. The detector uses Slang reflection to enumerate sampler descriptors, and uses an AST + reflection cross-reference to detect that the sampler is bound through a normal table slot rather than declared as `StaticSampler` in the root signature. **D3D12-relevant:** Vulkan binds samplers through descriptor sets without the static-sampler/heap distinction (immutable samplers exist but are a different mechanism), and Metal manages sampler-state objects through the argument-buffer system; this rule still surfaces a portability concern because the runtime cost of an unnecessary heap-resident sampler shows up as register pressure on every backend.

## Why it matters on a GPU

D3D12 distinguishes static (immutable, declared in the root signature) from dynamic (descriptor-table or root-descriptor) samplers. Static samplers are baked into the pipeline state at PSO creation, occupy *no* descriptor heap slot, and are pre-resident in the sampler unit on every IHV. Dynamic samplers consume a slot in the sampler descriptor heap (D3D12 caps sampler heaps at 2048 simultaneous descriptors), require a descriptor-table dereference at draw time, and on AMD RDNA 2/3 specifically are loaded into the sampler-state SGPR allocation per wave — competing with the rest of the SGPR budget that gates wave occupancy.

NVIDIA Turing and Ada Lovelace handle samplers via a separate per-SM sampler descriptor cache; static samplers warm that cache once at PSO bind time and stay resident, while dynamic samplers participate in the cache eviction policy. Intel Xe-HPG documents an analogous distinction. Across all three IHVs, the rule of thumb is: a sampler whose state never changes across draws of the same PSO should be declared static — there is no down-side beyond the requirement that the state be known at PSO creation.

The rule fires when reflection sees a sampler descriptor whose state is a constant configuration (no per-draw variability is reflected by the binding metadata). Real dynamic samplers — e.g. user-controlled anisotropy that the application toggles between draws — should not be flagged; the rule's heuristic is "unchanged across reflected call sites and bound as a non-static descriptor".

## Examples

### Bad

```hlsl
// Sampler with state that never varies across draws, but bound as a
// dynamic descriptor — pays a heap slot and a per-wave SGPR for nothing.
SamplerState LinearWrap : register(s0);

float4 sample(Texture2D<float4> t, float2 uv) {
    return t.Sample(LinearWrap, uv);
}
```

### Good

```hlsl
// Declare LinearWrap as a StaticSampler in the root signature; the shader
// declaration stays the same but the descriptor binds at PSO creation.
//
// In the application root signature:
//   D3D12_STATIC_SAMPLER_DESC linear_wrap = { ... };
//   root_sig.Init(..., 1, &linear_wrap);
//
// The shader-side declaration is unchanged:
SamplerState LinearWrap : register(s0);

float4 sample(Texture2D<float4> t, float2 uv) {
    return t.Sample(LinearWrap, uv);
}
```

## Options

none

## Fix availability

**suggestion** — Promoting a dynamic sampler to static requires a corresponding root-signature change on the application side, which the linter cannot make. The diagnostic identifies the sampler and the constant-state evidence; the author edits the root signature.

## See also

- Related rule: [comparison-sampler-without-comparison-op](comparison-sampler-without-comparison-op.md) — sampler descriptor type / call site mismatch
- Related rule: [cbuffer-large-fits-rootcbv-not-table](cbuffer-large-fits-rootcbv-not-table.md) — same descriptor-heap-vs-root-binding tradeoff for cbuffers
- D3D12 reference: `D3D12_STATIC_SAMPLER_DESC` and the static-sampler section of the root-signature documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/static-sampler-when-dynamic-used.md)

*© 2026 NelCit, CC-BY-4.0.*
