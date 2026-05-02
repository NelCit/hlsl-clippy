---
id: divergent-buffer-index-on-uniform-resource
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
---

# divergent-buffer-index-on-uniform-resource

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

An indexed buffer access `buf[i]` (where `buf` is a `Buffer`, `StructuredBuffer`, `ByteAddressBuffer`, or `ConstantBuffer<T>` and `i` is a wave-divergent expression) on a *resource binding* that is itself uniform across the wave — that is, `buf` is referenced through a single descriptor known at compile time, not through an `[NonUniformResourceIndex]` heap index. The hazard is the index, not the resource. The rule fires when the divergence analysis can prove the index varies across the wave (typical sources: `SV_DispatchThreadID`, per-lane loaded values, results of `WaveReadLaneAt`) while the resource itself is bound uniformly.

## Why it matters on a GPU

Modern GPUs split memory paths between the *scalar* / *constant* cache and the *vector* L1. On AMD RDNA 2/3, a uniform-resource + uniform-index buffer load is issued as a scalar load through the K$, returning one value to the SGPR file at a fraction of the cost of a vector load. A uniform-resource + *divergent*-index load is forced onto the vector path: the hardware issues 32 (wave32) or 64 (wave64) parallel L1 transactions, one per lane. On NVIDIA Ada the constant-cache fast path requires both the resource and the offset be uniform; a divergent offset spills to the global L1 / L2 path and serialises by cache-line. On Intel Xe-HPG, the constant-buffer fast path likewise requires uniform offsets and the divergent case falls through to the data-port path, which serialises across distinct cache lines.

The pattern is most painful for `ConstantBuffer<T>` and small `StructuredBuffer` accesses that the author intended as constant-time table lookups: a uniform binding seems to "promise" constant-cache behaviour, but the divergent index destroys that. The result is a memory-bound kernel that profilers attribute to the L1 (which then looks contended), when the real cause is that a divergent index forced the access off the scalar path. The fix is one of: (a) restructure the data so the indexed value is wave-uniform (often by hoisting the load outside the divergent code), (b) prove the index is wave-uniform via `WaveReadLaneFirst(i)` if the algorithm allows, or (c) move the table to a typed `Buffer<float4>` view designed for the vector cache so the access pattern matches the binding.

The rule shares the uniformity machinery with the locked `wave-active-all-equal-precheck` rule (per ADR 0011). The diagnostic distinguishes between "definitely divergent" and "could be divergent" indices and only fires on the former to keep false-positive rate manageable. On `[NonUniformResourceIndex]`-marked accesses the rule is silent — that case is handled by [non-uniform-resource-index](non-uniform-resource-index.md).

## Examples

### Bad

```hlsl
ConstantBuffer<MaterialParams> g_MaterialTable[64] : register(b0);  // uniform binding

float4 ps_main(float3 worldPos : POSITION, uint matId : MAT_ID) : SV_Target {
    // matId varies per pixel — divergent index on a uniform resource binding.
    // Forces every lane onto the vector cache path; loses the K$ fast path
    // that the ConstantBuffer view was bound for.
    MaterialParams p = g_MaterialTable[matId];
    return shade(worldPos, p);
}
```

### Good

```hlsl
// Move the table to a typed buffer view designed for divergent vector loads,
// which routes through L1 instead of the K$ and avoids the broken-fast-path tax.
StructuredBuffer<MaterialParams> g_MaterialTable : register(t0);

float4 ps_main(float3 worldPos : POSITION, uint matId : MAT_ID) : SV_Target {
    MaterialParams p = g_MaterialTable[matId];
    return shade(worldPos, p);
}

// Or, if the algorithm permits, reduce to a wave-uniform index first:
float4 ps_main_uniform(float3 worldPos : POSITION, uint matId : MAT_ID) : SV_Target {
    uint uniformMatId = WaveReadLaneFirst(matId);  // safe only if all lanes agree
    MaterialParams p = g_MaterialTable[uniformMatId];
    return shade(worldPos, p);
}
```

## Options

none

## Fix availability

**suggestion** — Three valid fixes exist (resource-view change, hoist-and-uniformise, or accept the cost). The diagnostic identifies the divergent index expression and the uniform binding so the author can choose; no automated rewrite is offered.

## See also

- Related rule: [non-uniform-resource-index](non-uniform-resource-index.md) — the inverse (divergent resource binding)
- Related rule: [cbuffer-divergent-index](cbuffer-divergent-index.md) — cbuffer-specific divergent index hazard
- Related rule: [wave-active-all-equal-precheck](wave-active-all-equal-precheck.md) — uniformity-test idiom
- HLSL intrinsic reference: `WaveReadLaneFirst`, `NonUniformResourceIndex`
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/divergent-buffer-index-on-uniform-resource.md)

*© 2026 NelCit, CC-BY-4.0.*
