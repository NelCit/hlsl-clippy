---
id: long-vector-bytebuf-load-misaligned
category: long-vectors
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# long-vector-bytebuf-load-misaligned

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `ByteAddressBuffer.Load<vector<T, N>>(offset)` with `N >= 5` whose constant-folded `offset` is not aligned to the natural alignment of the long-vector type (`N * sizeof(T)` rounded up to the IHV's preferred load width — 16 bytes for FP16/BF16, 32 bytes for FP32 long vectors with `N >= 8`). Constant-fold the offset; fire on misalignment.

## Why it matters on a GPU

Long-vector loads through `ByteAddressBuffer` lower to a sequence of widened scalar-cache fetches on every IHV. AMD RDNA 2/3's K$ delivers 64-byte cache lines; NVIDIA Ada's L1 scalar path is 128-byte; Intel Xe-HPG's scalar cache aligns to 64 bytes. When the long vector's start address is naturally aligned, the fetcher issues one or two cache-line transactions (depending on vector size); when it isn't, the fetcher issues N+1 transactions because the data straddles a line, and on stricter implementations the load may fault entirely.

The SM 6.9 spec marks misaligned long-vector loads as either degraded (split transactions) or undefined (fault), depending on implementation. NVIDIA's Ada driver runs the split path silently with no diagnostic; AMD RDNA 3's driver also runs split; future implementations may fault. Catching the misalignment at lint time prevents both the silent perf regression and the future-portability fault.

The fix is to round the offset up to the required alignment in the application's data layout. The rule is suggestion-tier because the alignment requirement varies slightly by component type; the diagnostic names the smallest safe alignment (16 bytes for FP16/BF16 long vectors, 32 bytes for FP32 long vectors with `N >= 8`).

## Examples

### Bad

```hlsl
// 8-element float load at offset 12 — straddles a 32-byte boundary.
ByteAddressBuffer g_Data : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    vector<float, 8> v = g_Data.Load<vector<float, 8> >(12);  // misaligned
}
```

### Good

```hlsl
// Offset 32 — aligned to the natural long-vector boundary.
ByteAddressBuffer g_Data : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    vector<float, 8> v = g_Data.Load<vector<float, 8> >(32);
}
```

## Options

none

## Fix availability

**suggestion** — The aligned offset is a function of the upload-side layout; the diagnostic names the offending offset and the required alignment.

## See also

- Related rule: [byteaddressbuffer-load-misaligned](byteaddressbuffer-load-misaligned.md) — same alignment principle for legacy 2/3/4-vector loads
- Related rule: [long-vector-typed-buffer-load](long-vector-typed-buffer-load.md) — companion long-vector spec rule
- Related rule: [coopvec-base-offset-misaligned](coopvec-base-offset-misaligned.md) — same alignment principle for cooperative-vector loads
- HLSL specification: [proposal 0030 DXIL vectors](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md)
- Companion blog post: [long-vectors overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/long-vector-bytebuf-load-misaligned.md)

*© 2026 NelCit, CC-BY-4.0.*
