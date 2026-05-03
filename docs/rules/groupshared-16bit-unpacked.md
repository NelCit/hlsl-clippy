---
id: groupshared-16bit-unpacked
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# groupshared-16bit-unpacked

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `groupshared` array whose element type is `min16float`, `min16uint`, `min16int`, `float16_t`, `uint16_t`, or `int16_t`, where every load site widens the value to 32 bits before any arithmetic. The detector uses Slang reflection to confirm the groupshared element width and the AST to confirm that no consuming expression uses a packed-math intrinsic (`dot2add`, `mul`/`mad` on a `min16float2` operand kept narrow, etc.). It fires when the storage is narrow but every consumer is wide. It does not fire when at least one consuming site keeps the value in 16 bits through a packed intrinsic.

## Why it matters on a GPU

AMD RDNA 2/3 packs two 16-bit values per VGPR lane and per LDS bank, but the packing only pays off when the entire data path stays narrow. The hardware exposes packed-math instructions (`v_pk_add_f16`, `v_pk_mul_f16`, `v_dot2_f16`) that consume two `float16` lanes per VGPR and produce two `float16` results in one issue slot. When source code stores `min16float` in groupshared but widens to `float` at the load site, the savings collapse: the LDS access still moves the narrow representation, but the immediately-following type promotion forces the value into a full-width VGPR before any arithmetic, and the compiler cannot recover the narrow packing without proving every consumer remains narrow. NVIDIA Turing introduced HFMA2 and Ada extends it; the same principle holds — the half2 / int16x2 instructions need narrow operands sitting in narrow registers, not promoted scalars.

The LDS side of the saving is real and measurable. A `groupshared float16_t Tile[1024]` occupies 2 KB; the same logical tile as `groupshared float Tile[1024]` occupies 4 KB. On RDNA 3 with 64 KB LDS per WGP, the difference of 2 KB per workgroup directly translates into one or two extra concurrent workgroups per WGP — a step in the occupancy curve. But the saving is meaningful only if the consumer also operates in 16 bits; otherwise the source has paid the storage savings in exchange for a widening conversion at every load and gained nothing in the math pipeline.

Intel Xe-HPG documents an analogous packed-half pipeline. Across all three IHVs, the rule of thumb is the same: if you're storing 16-bit values, keep them 16-bit through the consumer, or store 32-bit and skip the bookkeeping. The lint rule surfaces the half-and-half cases where the storage is narrow but the math is wide.

## Examples

### Bad

```hlsl
groupshared min16float Tile[64 * 64];

[numthreads(64, 1, 1)]
void cs_widening(uint gi : SV_GroupIndex) {
    // Load promotes to float at every site — packed-math savings lost.
    float a = (float)Tile[gi];
    float b = (float)Tile[gi + 1];
    Output[gi] = a + b * 0.5f;
}
```

### Good

```hlsl
groupshared min16float Tile[64 * 64];

[numthreads(64, 1, 1)]
void cs_narrow(uint gi : SV_GroupIndex) {
    // Stay in min16float through the math; lowers to v_pk_* on RDNA.
    min16float a = Tile[gi];
    min16float b = Tile[gi + 1];
    min16float r = a + b * (min16float)0.5;
    Output[gi] = (float)r;  // single widen at the store site is fine.
}

// Or, if the consumer needs full precision throughout, store full precision.
groupshared float TileWide[64 * 64];
```

## Options

none

## Fix availability

**suggestion** — Choosing between "keep math narrow" and "store wide" depends on the precision requirements of the consumer. The diagnostic flags the widen-at-load pattern; the author picks the resolution.

## See also

- Related rule: [groupshared-union-aliased](groupshared-union-aliased.md) — multiple typed views over the same groupshared offset
- Related rule: [groupshared-too-large](groupshared-too-large.md) — groupshared occupancy thresholds
- HLSL reference: `min16float`, `float16_t`, packed-math intrinsics in the DirectX HLSL Shader Model 6.2+ documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/groupshared-16bit-unpacked.md)

*© 2026 NelCit, CC-BY-4.0.*
