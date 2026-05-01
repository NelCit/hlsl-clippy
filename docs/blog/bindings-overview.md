---
title: "Where root signatures and descriptor heaps quietly cost you"
date: 2026-05-01
author: NelCit
category: bindings
tags: [hlsl, shaders, performance, bindings, root-signature, descriptor-heap, d3d12]
license: CC-BY-4.0
---

You have a compute shader that reads `g_MaterialTable[matId]`. `matId` came in
through a per-pixel attribute, which means it varies across the wave. The
binding looks innocent — `ConstantBuffer<MaterialParams> g_MaterialTable[64]`
in the root signature, one descriptor in a table, the same code that has
worked on every project for the last five years. The shader runs. The frame
ships. RGA shows the kernel is L1-bound and you wonder why a sixty-four entry
table is hot in L1.

It is not in L1 by mistake. The binding is uniform, but the index is
divergent, and a divergent index on a uniform binding silently kicks the
access off the scalar / constant cache fast path and onto the vector L1
path, one transaction per active lane. Thirty-two cache misses, in series,
for what the author wrote as a constant-time table lookup. The ISA shows the
truth — a `s_buffer_load_dword` would have been one issue; the divergent form
becomes thirty-two `buffer_load_dword`s through the vector path — but the
HLSL is unchanged from the version that did fit in scalar registers when the
index was wave-uniform. That is the shape of nearly every bindings footgun
on D3D12: the syntax is fine, the API call is fine, the perf is gone.

This post walks the four root signature parameter types, then digs into five
specific patterns that the bindings rules in `hlsl-clippy` flag and explains
the GPU mechanism behind each one. The companion [bindings rules
catalogue](/rules/) has the full list; the deep dives below cover the
mechanism buckets that account for most of the cost we have observed in real
codebases.

## The four root-signature parameter types

A D3D12 root signature is sixty-four DWORDs. Every parameter consumes some
of that budget, and the cost-per-access scales inversely with the budget
consumed:

- **Root constants**: inline DWORDs in the root signature. One DWORD per
  `uint` / `float`, no memory load on access — the values land directly in
  scalar registers (SGPRs on RDNA, uniform registers on NVIDIA) at wave
  launch. Cap of sixty-four DWORDs total minus everything else, so in
  practice a few dozen DWORDs of headroom.
- **Root descriptors (root CBV / root SRV / root UAV)**: a sixty-four-bit
  GPU virtual address inline in the root signature. Two DWORDs each. One
  memory load to fetch the resource payload — no descriptor-heap dereference.
  A root CBV access on RDNA is a single `s_load_dwordx{2,4}` from
  `SGPR_pair + offset`.
- **Descriptor tables**: one DWORD that points into a descriptor heap. The
  hardware loads the descriptor, then loads the resource payload from the
  descriptor's address. Two memory dereferences per access on the cold path
  (the descriptor itself caches well).
- **Static samplers**: declared in the root signature, baked into the PSO,
  occupy zero descriptor heap slots. Pre-resident in the sampler unit on
  every IHV.

The cost ordering is monotonic. Root constants are cheaper to access than
root descriptors which are cheaper than descriptor tables. The point of the
budget — and the hard part — is allocating it: a high-frequency CBV
demoted into a descriptor table and a rarely-read CBV promoted to a root
descriptor is a backwards layout that you can ship for years without
noticing, because the warm cache hides it on synthetic benchmarks but not on
the cold-cache transitions of the real frame.

## 1. Scalar K$ vs vector L1: divergent indices on uniform bindings

The first deep dive is the one that motivated the hook. AMD RDNA 2/3 has two
parallel L1-class caches: a scalar / constant cache (the K$, sixteen
kilobytes per CU, shared across waves) that backs SGPR loads, and a vector
L0 / L1 (sixteen kilobytes per WGP on RDNA 3) that backs VGPR loads. NVIDIA
Turing through Ada has an analogous split with a dedicated constant cache
backing uniform-buffer reads and the unified L1 backing texture and
structured-buffer reads. Intel Xe-HPG documents an analogous distinction
through its dataport and constant-buffer paths.

The rule for the scalar / constant path on every IHV is the same: both the
resource binding *and* the offset must be wave-uniform. If either is
divergent, the access falls through to the vector path. This is exactly
what happens when you write:

```hlsl
ConstantBuffer<MaterialParams> g_MaterialTable[64] : register(b0);

float4 ps_main(float3 worldPos : POSITION, uint matId : MAT_ID) : SV_Target
{
    MaterialParams p = g_MaterialTable[matId];
    return shade(worldPos, p);
}
```

The binding is uniform. `matId` is divergent. The K$ fast path is gone. The
shader now issues thirty-two parallel vector loads through L1, serialised by
cache line. The author's intent — a constant-time material table — is
unchanged from the source, but the hardware path is twenty to thirty times
slower.

The fix is one of three: hoist the load to wave-uniform context (which often
means restructuring the dispatch), reduce to a uniform index with
`WaveReadLaneFirst(matId)` if the algorithm tolerates it, or accept the
vector path and switch the resource type to `StructuredBuffer<MaterialParams>`
which was designed for it. Each is a different trade-off; the rule names the
problem and lets the author pick. See
[`/rules/divergent-buffer-index-on-uniform-resource`](/rules/divergent-buffer-index-on-uniform-resource).

## 2. NonUniformResourceIndex: the marker the spec demands

A close cousin is the inverse: a divergent index into an array of resources
on a heap, where the *binding itself* is divergent across the wave. SM 5.1
added `NonUniformResourceIndex` exactly for this case, and SM 6.6 made it a
first-class concern with `ResourceDescriptorHeap[i]` and
`SamplerDescriptorHeap[i]` direct heap access (the NRI / DynamicResource
surface that lets you skip the binding-table layer entirely).

The DXIL specification defines it as undefined behaviour to index a resource
array with a non-uniform value without the `NonUniformResourceIndex` marker.
The reason is architectural: descriptor resolution happens once per wave by
default. If all lanes use the same index, the driver emits one descriptor
load and broadcasts. If lanes disagree and the marker is missing, the driver
is permitted to use any active lane's value — which on AMD RDNA in practice
means lane zero — and silently feed every other lane the wrong resource.

```hlsl
Texture2D<float4> g_textures[] : register(t0, space1);

float4 ps_main(float2 uv : TEXCOORD0, uint matId : TEXCOORD1) : SV_Target
{
    return g_textures[matId].Sample(g_sampler, uv);  // missing marker
}
```

This compiles. It validates. In release mode it produces wrong textures on
divergent geometry, with no GPU-side error. The marker is the contract:

```hlsl
return g_textures[NonUniformResourceIndex(matId)].Sample(g_sampler, uv);
```

With the marker, the driver emits a waterfall loop — one iteration per
unique index value among the active lanes, with inactive lanes masked.
Slow, but correct. The same applies to SM 6.6 direct heap access:
`ResourceDescriptorHeap[NonUniformResourceIndex(i)]`. See
[`/rules/non-uniform-resource-index`](/rules/non-uniform-resource-index)
and the related descriptor-heap rule for SM 6.6 patterns. The marker is a
correctness fix, not a perf fix; the perf fix is grouping draws so the index
*is* uniform when you have the choice.

## 3. Root CBV vs descriptor table: small constants on the hot path

Now the root-signature shape question. A `cbuffer` that fits in a CBV
(D3D12 caps a CBV at 65536 bytes; the hardware-efficient range is much
smaller) and is read on every wave's hot path probably belongs as a root
CBV, not a descriptor-table CBV. On RDNA, a root CBV resolves to a single
scalar K$ load from `SGPR_pair + offset`. A descriptor-table CBV resolves
to a scalar load to fetch the descriptor, *then* a scalar load to fetch the
payload — one extra K$ transaction and one extra serial dependency at the
start of every cbuffer access. NVIDIA Turing through Ada documents the same
extra dereference through the constant cache. Intel Xe-HPG handles the
indirection through its scalar load path.

The cost is small per access. The cost is multiplied across every wave on
every CU/SM that consumes the cbuffer. For a per-frame `FrameConstants` that
every draw and every dispatch reads, that is millions of accesses per frame
on a busy renderer. The fix is a one-line root-signature change:

```cpp
// before — descriptor table:
D3D12_DESCRIPTOR_RANGE cbv_range = {
    D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, 0
};
root_param.InitAsDescriptorTable(1, &cbv_range);

// after — root CBV (2 DWORDs of root-signature budget):
root_param.InitAsConstantBufferView(0);
```

The HLSL declaration is unchanged. The shader-side code is unchanged. Only
the root-signature wiring changes. See
[`/rules/cbuffer-large-fits-rootcbv-not-table`](/rules/cbuffer-large-fits-rootcbv-not-table)
for the reflection-driven detector. The trade-off: root CBVs cost two DWORDs
each out of sixty-four, and on consoles with tight root-signature budgets
that math gets crowded fast. The rule is "use this when you have the budget
and the cbuffer is hot", not a universal mandate.

The same descriptor-heap-vs-root-binding tension surfaces for samplers. A
sampler whose state never varies across draws of the same PSO is a
`StaticSampler` — declared in the root signature, baked into the PSO, no
descriptor heap slot consumed, pre-resident in the sampler unit on every
IHV. Declaring such a sampler as a normal heap-resident `SamplerState` pays
a heap slot and a per-wave SGPR allocation for nothing. See
[`/rules/static-sampler-when-dynamic-used`](/rules/static-sampler-when-dynamic-used).

## 4. RWResource read-only abuse

A `RWStructuredBuffer<uint>` declared but only ever read — never written, never
passed to `InterlockedAdd`, no assignment through the `[]` operator — is a
common pattern in shaders that started life with write paths and lost them
during refactor. The HLSL compiles, the runtime accepts it, and the driver
loses two optimisations:

First, the cache coherency path differs between SRVs and UAVs. An SRV is
read-only at the hardware level; the driver knows no write will invalidate
cached data during the dispatch and applies aggressive caching, prefetching,
and (on AMD RDNA) Delta Colour Compression on textures. A UAV must be
treated as potentially written; DCC is suppressed, the cache must consider
each access a coherence event, prefetching backs off.

Second, on D3D12 a UAV has stricter resource-state requirements than an SRV
— UAV barriers are required between dispatches that write the resource, and
the runtime tracks state transitions accordingly. A read-only UAV pays the
state-tracking overhead and the per-IHV coherency cost for nothing.

The fix is to demote: rename `RWBuffer` to `Buffer`, `RWTexture2D` to
`Texture2D`, `RWStructuredBuffer` to `StructuredBuffer`, and move the
binding from a `u` register to a `t` register. The shader is mechanical to
update; the application-side root signature, PSO descriptor table, and
barrier code must change in concert. See
[`/rules/rwresource-read-only-usage`](/rules/rwresource-read-only-usage).

## 5. Cbuffer size and layout traps

The last bucket is the cbuffer-as-data-structure problem. Three rules cover
the surface:

[`/rules/oversized-cbuffer`](/rules/oversized-cbuffer) fires on `cbuffer`
declarations larger than a configurable threshold (default four kilobytes).
The constant-cache working set is bounded — sixteen kilobytes K$ per CU on
RDNA, eight kilobytes constant cache per SM on NVIDIA — and a four-kilobyte
cbuffer almost certainly evicts itself on any wave-parallel dispatch. The
common cause is the "mega-cbuffer" pattern: per-material, per-frame, and
per-draw constants accumulated into one block to minimise descriptor-table
binding cost. The fix is usually to split the cbuffer (per-frame stays as
root CBV, per-draw moves to root constants, per-material moves to a
`StructuredBuffer` that hits the texture cache instead).

[`/rules/unused-cbuffer-field`](/rules/unused-cbuffer-field) fires on fields
declared inside a `cbuffer` and never read in any reachable shader function.
Reflection enumerates the fields by name and offset; AST scan confirms the
absence of references. An unused field still occupies cache lines; the GPU
loads the contiguous block. On AMD RDNA, each unused field that pushes the
cbuffer's live region across a cache-line boundary increases the number of
lines the cbuffer occupies on the K$. The fix is machine-applicable —
deleting an unused field declaration is a safe textual deletion — though the
CPU-side fill code that uploads the field should also be cleaned up.

[`/rules/structured-buffer-stride-mismatch`](/rules/structured-buffer-stride-mismatch)
and the related
[`/rules/structured-buffer-stride-not-cache-aligned`](/rules/structured-buffer-stride-not-cache-aligned)
catch the corresponding hazard for `StructuredBuffer<T>`. The first fires on
strides not a multiple of sixteen (a `float3 pos` struct, twelve bytes,
where every fourth element straddles a sub-cacheline boundary on RDNA's
sixteen-byte-aligned scatter/gather unit). The second is more selective: it
fires on strides that *are* multiples of four but not multiples of the
configured cache-line target — the canonical offender being twenty-four
bytes (`float3 + float2` packs to twenty-four after vec-alignment), where
three elements fit in a sixty-four-byte RDNA L1 line but the fourth
straddles. The fix is padding (one extra `float`) or restructuring to SoA
(`StructuredBuffer<float3> Positions` plus `StructuredBuffer<float2> UVs`)
when the consumer reads only some fields per pass.

## What reflection knows

Most of these rules are reflection-driven, not AST-driven. The pattern
`buf[i]` is the same in source whether `buf` is a uniform binding or a heap
descriptor; only Slang's reflection knows the binding kind and only Slang's
uniformity analysis knows whether `i` is divergent. The Phase 3
infrastructure that plumbs Slang reflection into `RuleContext` is
[ADR 0012](https://github.com/NelCit/hlsl-clippy/blob/main/docs/decisions/0012-phase-3-reflection-infrastructure.md);
it lazily builds a per-source reflection cache, exposes a typed
`ReflectionInfo` to rules, and confines `<slang.h>` to a single
implementation file. The bindings rules above all live on top of that
surface.

## Run it

The bindings pack lands in v0.3.0 alongside the rest of the Phase 3 rules.
Until then the rule docs above describe the design intent, and the math /
saturate-redundancy packs from v0.2.0 give you a working binary to point at
your shaders today. The pattern in every case is the same: a
reflection-driven detector identifies the binding shape, a uniformity check
(or a size threshold, or an access-pattern audit) confirms the hazard, and
the diagnostic names the rule and points at the doc page that explains the
GPU mechanism. Suppress per-line when you have measured the cost on your
target hardware and decided the trade-off is acceptable; take the fix when
you have not.

The eighteen-month-old shader folder is full of these. The mega-cbuffer that
grew over four releases. The `RWBuffer` whose write path was deleted in the
last refactor. The `g_textures[matId]` that was uniform when the renderer
was forward and divergent when it became deferred. None of them break the
build. All of them cost frames. The bindings rules pull the cost forward to
lint time so you do not learn about it from a profiler at three in the
morning before ship.

---

`hlsl-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/hlsl-clippy](https://github.com/NelCit/hlsl-clippy). If
you have encountered a binding-shape pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
