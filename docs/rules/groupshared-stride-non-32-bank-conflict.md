---
id: groupshared-stride-non-32-bank-conflict
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# groupshared-stride-non-32-bank-conflict

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

Index expressions into `groupshared` arrays of the form `arr[tid * S + k]`, `arr[gi * S + k]`, `arr[(tid << B) | k]`, or 2D access `arr[i][j]` where the inner-dimension stride `S` is in {2, 4, 8, 16, 64} — that is, any stride sharing a non-trivial GCD with 32. The companion rule [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) catches the worst case (stride exactly a multiple of 32); this rule catches the partial conflicts that still serialise the wave 2-, 4-, 8-, or 16-way.

## Why it matters on a GPU

GPU groupshared / LDS memory exposes 32 parallel banks of 4 bytes each on AMD RDNA 2/3 and on NVIDIA Turing / Ada / Blackwell. A wave of 32 lanes hitting 32 distinct banks completes in one cycle; any collision serialises the access. The conflict factor for a constant stride `S` is `32 / gcd(S, 32)` — so stride 2 yields a 2-way conflict (2x slowdown), stride 4 a 4-way (4x), stride 8 an 8-way (8x), stride 16 a 16-way (16x), and stride 64 a 32-way (32x — same as stride 32, because 64 mod 32 is 0). On Intel Xe-HPG the SLM exposes 16 banks at 4 bytes; the same modular arithmetic applies with a bank count of 16, so a stride-of-16 index is the worst case there but stride-2 / 4 / 8 still partially conflict.

Concretely: a structured-of-arrays access pattern where each lane reads a scalar at `gs[lane * 4]` (a four-float record per thread, lane reads the first field) collides with three other lanes on the same bank, halving and then halving again the effective LDS bandwidth. The optimiser cannot fix this — bank assignment is determined by the address modulo 32, which is a function of the index expression the shader writer chose. The fix is the same +1 padding trick documented for the stride-32 case (round the inner dimension up to a coprime-with-32 size, e.g. `[N][33]` instead of `[N][32]`, or interleave fields so adjacent lanes touch adjacent banks).

The rule fires only when the stride is a compile-time constant in the index expression. Variable strides may be benign at runtime; surfacing them would produce too many false positives until uniformity analysis can prove the stride. The diagnostic reports the conflict factor (2x, 4x, 8x, 16x) so the author can prioritise — a 2-way conflict on a cold path is rarely worth a refactor; a 16-way conflict on a transpose is.

## Examples

### Bad

```hlsl
// Each thread holds 4 floats; reading field 0 across the wave puts every lane
// on the same bank quartet. 4-way conflict, 4x slowdown on this access.
groupshared float g_PerThread[64 * 4];

[numthreads(64, 1, 1)]
void cs_aos_field0(uint gi : SV_GroupIndex) {
    g_PerThread[gi * 4 + 0] = (float)gi;
    GroupMemoryBarrierWithGroupSync();
    // Lane i reads address (i * 4) — banks 0, 4, 8, 12, 16, 20, 24, 28
    // (8 distinct banks across 32 lanes), 4-way conflict.
    float v = g_PerThread[gi * 4 + 0];
    Out[gi] = v * 2.0;
}
```

### Good

```hlsl
// Structure-of-arrays: field 0 lives in its own contiguous block. Lane i
// reads address i — banks 0..31 — one cycle, no conflict.
groupshared float g_Field0[64];
groupshared float g_Field1[64];
groupshared float g_Field2[64];
groupshared float g_Field3[64];

[numthreads(64, 1, 1)]
void cs_soa_field0(uint gi : SV_GroupIndex) {
    g_Field0[gi] = (float)gi;
    GroupMemoryBarrierWithGroupSync();
    float v = g_Field0[gi];
    Out[gi] = v * 2.0;
}
```

## Options

- `bank-count` (integer, default: 32) — the assumed LDS bank count. Set to 16 if targeting Intel Xe-HPG-style SLM where the bank count is half.
- `min-conflict-factor` (integer, default: 2) — the minimum conflict factor that triggers the diagnostic. Set higher to silence partial conflicts on cold paths.

## Fix availability

**suggestion** — Restructuring an array-of-structures to a structure-of-arrays (or applying +1 padding to the inner dimension) changes the LDS layout and may interact with other accesses in the same shader. The diagnostic reports the conflict factor and the offending stride so the author can choose between SoA conversion, padding, or a stride-bumping transformation.

## See also

- Related rule: [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) — the worst-case 32-way conflict
- Related rule: [groupshared-union-aliased](groupshared-union-aliased.md) — typed-view aliasing on the same LDS offset
- Related rule: [groupshared-16bit-unpacked](groupshared-16bit-unpacked.md) — packed-math LDS opportunities
- NVIDIA CUDA C++ Best Practices Guide: shared memory bank conflicts
- AMD GPUOpen: RDNA performance guide — LDS section
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/groupshared-stride-non-32-bank-conflict.md)

*© 2026 NelCit, CC-BY-4.0.*
