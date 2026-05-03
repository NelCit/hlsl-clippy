---
id: buffer-load-width-vs-cache-line
category: bindings
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# buffer-load-width-vs-cache-line

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A scalar `Load` (or `operator[]` returning a single element) on a `ByteAddressBuffer` / `StructuredBuffer<T>` whose access pattern aggregates across a wave to a contiguous cache-line-sized region. Each lane is loading 4 bytes, the wave's lanes are loading consecutive 4-byte slots, and the total fetch size matches a 64-byte (RDNA cache line) or 128-byte (Turing/Ada L1 line) transaction that would coalesce into a single `Load4` per lane group. The Phase 7 IR-level per-wave aggregation analysis identifies the pattern.

## Why it matters on a GPU

GPU memory hierarchies are designed for wide transactions per wave. AMD RDNA 2/3's L1 vector cache delivers data in 64-byte cache lines; NVIDIA Turing/Ada Lovelace's L1 caches in 128-byte lines; Intel Xe-HPG's L1 sampler/UAV cache uses 64-byte lines. When a wave's lanes issue 32 (RDNA wave32, NVIDIA, Xe-HPG) or 64 (RDNA wave64) per-lane scalar loads at consecutive byte offsets, the memory subsystem either coalesces them into one or two cache-line transactions automatically (best case) or issues N separate transactions (worst case, when the compiler cannot prove coalescibility).

The same data fetched as `Load4` per group of four lanes is unambiguous: the load unit issues exactly one transaction per `Load4` group, with no coalescing analysis needed. AMD RDNA's compiler documentation reports up to 30% perf improvement on bandwidth-bound kernels when per-lane scalar loads are rewritten as `Load4` aggregations; NVIDIA's CUDA programming guide reports similar magnitudes for the analogous `__ldg` width promotion.

The fix is to rewrite the per-lane scalar load as a `Load4` (or wider) load that fetches the whole cache line and indexes into the result by lane ID. The rule is suggestion-tier because the rewrite changes the access pattern — register pressure goes up because each lane now holds a wider value — and the trade-off is profitable only when the bandwidth savings dominate. The Phase 7 IR-level analysis estimates both sides.

This rule's specific cycle counts are speculative — exact savings depend on driver-level coalescing heuristics that vary by IHV and driver version. The diagnostic uses "may" language and emits the candidate rewrite as a comment.

## Examples

### Bad

```hlsl
// Per-lane scalar load: 32 transactions per wave for one cache line.
ByteAddressBuffer g_Data : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_GroupThreadID) {
    uint v = g_Data.Load(tid * 4);
    /* ... */
}
```

### Good

```hlsl
// Wide load: 8 transactions per wave (one Load4 per group of 4 lanes).
ByteAddressBuffer g_Data : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_GroupThreadID) {
    uint groupBase = (tid & ~3u) * 4;
    uint4 v4 = g_Data.Load4(groupBase);
    uint v = v4[tid & 3u];
    /* ... */
}
```

## Options

- `min-wave-coalesce` (integer, default: `32`) — minimum number of contiguous-byte lanes required before the rule fires.

## Fix availability

**suggestion** — The wide-load rewrite changes both the access pattern and the register pressure; the trade-off is profitable only on bandwidth-bound kernels. The diagnostic emits the candidate rewrite as a comment.

## See also

- Related rule: [byteaddressbuffer-load-misaligned](byteaddressbuffer-load-misaligned.md) — companion alignment rule on the wide-load form
- Related rule: [byteaddressbuffer-narrow-when-typed-fits](byteaddressbuffer-narrow-when-typed-fits.md) — companion buffer-cache-path rule
- Related rule: [structured-buffer-stride-not-cache-aligned](structured-buffer-stride-not-cache-aligned.md) — companion cache-line rule
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/buffer-load-width-vs-cache-line.md)

*© 2026 NelCit, CC-BY-4.0.*
