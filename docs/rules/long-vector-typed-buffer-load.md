---
id: long-vector-typed-buffer-load
category: long-vectors
severity: error
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# long-vector-typed-buffer-load

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `Buffer<vector<T, N>>` declaration with `N >= 5`, or a typed-buffer `Load`/`operator[]` whose returned type is a long vector. The SM 6.9 long-vector spec restricts typed-buffer (`Buffer<T>` / `RWBuffer<T>`) views to the legacy 1/2/3/4-wide vector types because typed-buffer fetches go through the texture cache, which expects DXGI-format-compatible types. Long vectors must be loaded through `ByteAddressBuffer` or `StructuredBuffer`. Slang reflection identifies the resource type at binding sites; the rule fires when the typed-view's element type is a long vector.

## Why it matters on a GPU

Typed buffers (`Buffer<float4>`, `RWBuffer<uint2>`, etc.) are texture-cache-backed views: the texture units on every IHV (NVIDIA's L1, AMD RDNA's TC, Intel Xe-HPG's L1 sampler cache) accept the load with a DXGI format descriptor, which the hardware uses to pick the right fetch shape and the right format conversion. Long vectors have no DXGI format equivalent — there is no `DXGI_FORMAT_R32G32B32A32B32C32D32E32F32_FLOAT` because the texture cache hardware was never designed for 32-byte fetches. The DXC validator rejects the type combination at PSO compile.

Slang and DXC both surface this as a clean error; the lint catches it earlier and points the author at the right resource type. `ByteAddressBuffer` (raw, untyped) and `StructuredBuffer<MyStruct>` (struct-typed, raw) both go through the K$ / scalar-data cache rather than the texture cache and have no DXGI-format restriction. They are the right targets for long-vector data.

The performance trade-off (texture cache vs. scalar cache) is real and is captured by the sibling rule `byteaddressbuffer-narrow-when-typed-fits` (perf-tier suggestion to move 1/2/3/4-vec data to typed views when it fits). For long vectors, the choice is forced: typed views are not an option.

## Examples

### Bad

```hlsl
// Typed buffer cannot hold a long vector.
Buffer<vector<float, 8>> g_LongVecData : register(t0);  // ERROR: typed long vec

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    vector<float, 8> v = g_LongVecData[tid];   // load via typed view
}
```

### Good

```hlsl
// StructuredBuffer / ByteAddressBuffer for long-vector data.
struct LongVec { vector<float, 8> v; };
StructuredBuffer<LongVec> g_LongVecData : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    vector<float, 8> v = g_LongVecData[tid].v;
}
```

## Options

none

## Fix availability

**suggestion** — Switching the resource type requires a corresponding application-side change to the descriptor; the diagnostic names the offending declaration and the recommended replacement.

## See also

- Related rule: [long-vector-bytebuf-load-misaligned](long-vector-bytebuf-load-misaligned.md) — companion long-vector load rule
- Related rule: [long-vector-non-elementwise-intrinsic](long-vector-non-elementwise-intrinsic.md) — companion long-vector spec rule
- Related rule: [byteaddressbuffer-narrow-when-typed-fits](byteaddressbuffer-narrow-when-typed-fits.md) — opposite-direction perf rule for short-vector data
- HLSL specification: [proposal 0030 DXIL vectors](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/long-vector-typed-buffer-load.md)

*© 2026 NelCit, CC-BY-4.0.*
