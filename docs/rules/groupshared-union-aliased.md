---
id: groupshared-union-aliased
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# groupshared-union-aliased

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `groupshared` declaration whose layout exposes two distinct typed views over the same byte offset, either via a manual `asuint`/`asfloat` round-trip pattern (writing as one type and reading as another against the same `groupshared` cell) or via a struct-hack that places different-typed fields at the same logical offset. The detector uses Slang reflection's groupshared layout to identify aliased offsets and matches the AST round-trip pattern at the access site. It does not fire on a single-type groupshared array nor on `asuint`/`asfloat` round-trips against locals or buffers (only against `groupshared` storage).

## Why it matters on a GPU

LDS (local data store / shared memory) on every desktop GPU is a typeless byte-addressable scratchpad with a small bank-aware optimiser. AMD RDNA 2/3 LDS is 64 KB per WGP organised as 32 banks of 4 bytes; NVIDIA Turing/Ada shared memory is 96 KB or 128 KB per SM in 32 banks of 4 bytes; Intel Xe-HPG SLM is 128 KB per Xe-core in 32 banks. The compiler reasons about LDS access patterns to schedule loads, fold redundant reads, and elide write-then-read traffic when the result can stay in registers. That reasoning relies on a stable type at each offset: when the HLSL source aliases two types across the same cell, the compiler must conservatively assume any write may invalidate any read, regardless of which type is in flight.

The fallback under aliasing is to round-trip every access through LDS memory rather than registers. A `groupshared float Tile[256]` accessed exclusively as `float` keeps its hot lanes in VGPRs across read-modify-write sequences; the same storage aliased with a `uint` view forces every read after every write to re-issue an LDS load even when the value just landed in a register. On a wave executing a tile-reduce or scan with a few read-modify-write iterations, the lost LDS-to-register caching converts what should be one or two LDS round-trips into a per-iteration round-trip — multiplying LDS pressure by the iteration count.

The aliasing pattern most often appears as an `asuint` / `asfloat` round-trip used to repurpose the same groupshared scratch for a different stage of a compute pass (e.g. accumulate as `uint` for atomic sums, then read back as `float`). The fix is usually to declare two separate `groupshared` arrays at distinct offsets — LDS is large enough on every modern part to absorb the duplication, and the optimiser regains its ability to schedule each view independently.

## Examples

### Bad

```hlsl
groupshared uint Scratch[256];

[numthreads(64, 1, 1)]
void cs_aliased(uint gi : SV_GroupIndex) {
    // Write as uint via atomic accumulation...
    InterlockedAdd(Scratch[gi], 1u);
    GroupMemoryBarrierWithGroupSync();
    // ...then read back as float through the same cell.
    float v = asfloat(Scratch[gi]);
    Output[gi] = v;
}
```

### Good

```hlsl
groupshared uint  Counts[256];
groupshared float Values[256];

[numthreads(64, 1, 1)]
void cs_distinct(uint gi : SV_GroupIndex) {
    InterlockedAdd(Counts[gi], 1u);
    GroupMemoryBarrierWithGroupSync();
    Values[gi] = float(Counts[gi]);
    Output[gi] = Values[gi];
}
```

## Options

none

## Fix availability

**suggestion** — Splitting the aliased storage typically requires renaming and updating every reader. The diagnostic identifies the aliased offset and the two views; the author chooses the rename.

## See also

- Related rule: [groupshared-16bit-unpacked](groupshared-16bit-unpacked.md) — 16-bit groupshared storage that widens at every access
- Related rule: [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) — bank conflicts on stride-32 access
- HLSL reference: `groupshared` storage class in the DirectX HLSL Shader Model 6.x documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-union-aliased.md)

*© 2026 NelCit, CC-BY-4.0.*
