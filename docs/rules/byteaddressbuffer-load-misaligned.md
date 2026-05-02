---
id: byteaddressbuffer-load-misaligned
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# byteaddressbuffer-load-misaligned

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `Load2`, `Load3`, or `Load4` call on a `ByteAddressBuffer` (or `RWByteAddressBuffer`) where the byte offset argument is a compile-time constant that does not satisfy the natural-alignment rule for the widened load: 8-byte alignment for `Load2`, 4-byte alignment for the underlying DWORDs but typically 16-byte alignment for `Load3`/`Load4` to land in a single cache line. The detector folds constant-arithmetic offsets (literal + literal, `K * sizeof(uint)` patterns) and also fires on the obvious `buf.Load4(13)`, `buf.Load2(6)`, and `buf.Load4(4 + 1)` shapes. It does not fire on offsets backed by a runtime variable; that case is reserved for a Phase 4 uniformity-aware follow-up.

## Why it matters on a GPU

`ByteAddressBuffer` widened loads compile to a single `BUFFER_LOAD_DWORDX{2,3,4}` (RDNA) or `LDG.E.{64,128}` (NVIDIA Turing/Ada) memory instruction. The hardware paths assume the address is naturally aligned to the load width: 8 bytes for an `x2`, 16 bytes for `x4`. AMD RDNA 2/3 documents that misaligned vector loads are *split* by the memory pipeline into the corresponding number of single-DWORD transactions — a `Load4` at offset 13 turns into four serial 4-byte loads instead of one 16-byte load. NVIDIA Ada Lovelace's L1 likewise penalises sub-line-aligned widened loads by replaying the access; Intel Xe-HPG's URB load path documents an equivalent fallback.

The cost is not just the extra issue slots. A 16-byte `Load4` that lands in one cache line costs one L1 tag lookup; a misaligned `Load4` that straddles a 64-byte (RDNA) or 128-byte (Ada) cache line doubles the tag lookups and burns through the cache miss-status holding registers (MSHRs) that bound how many in-flight memory requests a wave can have. On a wave doing several misaligned loads per iteration, the MSHR pressure caps memory-level parallelism and stalls subsequent loads behind the misaligned ones.

Because `ByteAddressBuffer` accepts arbitrary byte offsets by API design, the compiler will not flag the misalignment — it lowers the load faithfully. The rule surfaces the constant-offset cases at lint time so the developer can pad the source struct to the next alignment boundary, switch to a `StructuredBuffer<T>` view (whose stride enforces alignment), or adjust the offset arithmetic.

## Examples

### Bad

```hlsl
ByteAddressBuffer Bytes : register(t0);

float4 fetch_misaligned() {
    // Load4 at offset 13 — straddles the 16-byte boundary; splits into
    // four single-DWORD loads on RDNA 2/3 and replays on Ada.
    return asfloat(Bytes.Load4(13));
}

float2 fetch_misaligned_pair(uint base_dword) {
    // Load2 at a 4-byte-aligned offset is still misaligned for an x2 load
    // (needs 8-byte alignment to issue as a single DWORDX2).
    return asfloat(Bytes.Load2(base_dword * 4 + 4));
}
```

### Good

```hlsl
ByteAddressBuffer Bytes : register(t0);

float4 fetch_aligned() {
    // 16-byte aligned — single BUFFER_LOAD_DWORDX4.
    return asfloat(Bytes.Load4(16));
}

float2 fetch_aligned_pair(uint base_dword2) {
    // base_dword2 is in units of 2 DWORDs (8 bytes) — naturally aligned.
    return asfloat(Bytes.Load2(base_dword2 * 8));
}
```

## Options

none

## Fix availability

**suggestion** — The fix typically requires repacking the source data layout or changing the addressing scheme, which has CPU-side implications. The diagnostic reports the observed offset and the next valid aligned offset; the author chooses the resolution.

## See also

- Related rule: [byteaddressbuffer-narrow-when-typed-fits](byteaddressbuffer-narrow-when-typed-fits.md) — prefer typed views when the access pattern matches
- Related rule: [structured-buffer-stride-not-cache-aligned](structured-buffer-stride-not-cache-aligned.md) — stride alignment for typed structured buffers
- HLSL intrinsic reference: `ByteAddressBuffer::Load2`, `Load3`, `Load4` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/byteaddressbuffer-load-misaligned.md)

*© 2026 NelCit, CC-BY-4.0.*
