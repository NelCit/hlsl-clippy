---
id: cbuffer-large-fits-rootcbv-not-table
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# cbuffer-large-fits-rootcbv-not-table

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `cbuffer` (or `ConstantBuffer<T>`) whose total size, as reported by Slang reflection, fits within the D3D12 root CBV size limit (a CBV may address up to 65536 bytes per the D3D12 spec) and that is referenced once per dispatch / draw — i.e. the binding does not require an array of CBVs swept by an index — but is currently bound through a descriptor table rather than as a root CBV. The detector reads the binding kind from reflection (descriptor table vs root CBV vs root constants) and matches against the cbuffer size and access pattern. **D3D12-relevant:** Vulkan binds uniform buffers via descriptor sets without the root-vs-table distinction (push descriptors are the closest equivalent), and Metal handles small constant buffers via setBytes/setBuffer on the encoder; this rule still surfaces a portability concern because the descriptor-indirection cost shows up as extra indirect loads on every backend, even when the API surface differs.

## Why it matters on a GPU

D3D12's root signature distinguishes three binding kinds for cbuffers, in increasing indirection: root constants (inline DWORDs in the root signature, no memory load), root CBVs (a 64-bit GPU virtual address inline in the root signature, one memory load to fetch the cbuffer data), and descriptor-table CBVs (a heap offset in the root signature, one memory load to fetch the descriptor + one to fetch the cbuffer data). The descriptor-table path costs an extra memory dereference per cbuffer access compared to the root-CBV path.

On AMD RDNA 2/3, the root signature lives in SGPRs at the start of every wave; a root CBV resolves to "load from `SGPR_pair`+offset" — one scalar K$ load. A descriptor-table CBV resolves to "load descriptor from heap, then load from descriptor's address+offset" — one scalar load to fetch the descriptor and another to fetch the cbuffer payload. The extra load competes for K$ bandwidth and adds a serial dependency at the start of every cbuffer access. NVIDIA Turing/Ada document an analogous extra dereference for descriptor-table CBVs versus root CBVs, paid through the constant cache. Intel Xe-HPG handles the extra indirection through its scalar load path.

The cost is small per access but multiplied across every wave on every CU/SM that consumes the cbuffer. For a per-frame `FrameConstants` cbuffer accessed by every draw and every compute dispatch, the descriptor-table indirection runs millions of times per frame on a busy renderer. The fix is a one-line root-signature change: declare the binding as `RootCBV` instead of a slot in a descriptor table. The shader-side declaration is unchanged. The constraint is that root-signature space is finite (64 DWORDs total, with each root CBV consuming 2 DWORDs and each descriptor-table entry consuming 1), so the rule is a "use this when you have the root-signature budget" suggestion, not a universal mandate.

## Examples

### Bad

```hlsl
// Bound through a descriptor table — extra dereference per access.
// Application root-signature snippet (paraphrased):
//   D3D12_DESCRIPTOR_RANGE cbv_range = {
//       D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, 0
//   };
//   root_param.InitAsDescriptorTable(1, &cbv_range);
cbuffer FrameConstants : register(b0) {
    float4x4 ViewProj;
    float4   CameraPos;
    float    Time;
    // ... ~256 bytes total ...
};
```

### Good

```hlsl
// Same shader-side declaration; root-signature changes the binding kind.
// Application root-signature snippet:
//   root_param.InitAsConstantBufferView(0);  // root CBV, 2 DWORDs
cbuffer FrameConstants : register(b0) {
    float4x4 ViewProj;
    float4   CameraPos;
    float    Time;
    // ... ~256 bytes total ...
};
```

## Options

none

## Fix availability

**suggestion** — Promoting a cbuffer to a root CBV requires editing the application's root signature, which the linter cannot make. The diagnostic identifies the cbuffer and the binding-kind opportunity; the author confirms root-signature budget is available.

## See also

- Related rule: [cbuffer-fits-rootconstants](cbuffer-fits-rootconstants.md) — even smaller cbuffers fit as root constants
- Related rule: [static-sampler-when-dynamic-used](static-sampler-when-dynamic-used.md) — same descriptor-heap-vs-root-binding tradeoff for samplers
- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — cbuffer layout efficiency
- D3D12 reference: `D3D12_ROOT_PARAMETER_TYPE_CBV` and the root-signature size budget documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/cbuffer-large-fits-rootcbv-not-table.md)

*© 2026 NelCit, CC-BY-4.0.*
