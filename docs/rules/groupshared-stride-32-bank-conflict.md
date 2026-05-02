---
id: groupshared-stride-32-bank-conflict
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# groupshared-stride-32-bank-conflict

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Declarations of `groupshared` arrays accessed with an index expression of the form `tid * 32 + k`, `gi * 32 + k`, `tid << 5 | k`, or any constant stride that is a multiple of 32 (32, 64, 128, ...). The same pattern triggers for 2D groupshared arrays declared `[N][32]` (or `[N][64]`) and accessed `Tile[i][j]` — the second dimension being a power-of-two multiple of 32 forces every column access to land in the same LDS bank.

## Why it matters on a GPU

GPU groupshared / LDS memory is internally organised into 32 parallel banks (sometimes called "channels"). On AMD RDNA 2/3, the LDS hardware has 32 banks; each bank serves one 32-bit word per cycle, and a wave of 32 lanes accessing 32 distinct banks completes in one clock. On NVIDIA Turing, Ada, and Blackwell, shared memory has 32 banks at 4 bytes wide each, and a warp of 32 threads touching 32 distinct banks likewise completes in one clock. On Intel Xe-HPG, the SLM banks are 16-wide but the same conflict mechanic applies. When two or more lanes in the same wave/warp hit the same bank in the same cycle, the hardware serialises the accesses: a 2-way conflict doubles the latency, a 32-way conflict serialises the entire wave to 32 sequential cycles, a full 32x slowdown on that load or store.

The stride-of-32 pattern is the canonical worst case. If lane `i` accesses `Tile[i * 32 + k]`, then lanes `0, 1, 2, ..., 31` all hit byte offsets `0, 32*4, 64*4, ..., 31*32*4` — that is, addresses `0, 128, 256, ..., 3968`. Mapping those byte addresses to banks (`(addr / 4) mod 32`), they all land in bank 0. A 32-way bank conflict serialises the wave, taking 32 cycles for what should have been 1 cycle. The same pattern appears with explicit 2D `groupshared float Tile[N][32]` indexed `Tile[i][gi]` where `gi` is the SV_GroupIndex along the inner stride: every lane reads column `gi` from a different row, but rows are 32 floats apart, so all 32 lanes hit one bank.

The standard mitigation is "+1 padding" — declare the array one element wider than the natural stride, e.g. `groupshared float Tile[N][33]` instead of `[N][32]`. Now lane `i` accessing `Tile[i][gi]` reads address `i * 33 + gi`; mapping to banks gives `(i * 33 + gi) mod 32 = (i + gi) mod 32` since 33 mod 32 is 1, distributing accesses uniformly across all 32 banks. The cost is one extra float per row of LDS — typically 0.4-0.8% of the LDS budget — and the win is up to 32x throughput on every cross-stride access. This trick has been standard practice in NVIDIA CUDA shared-memory transposes since 2008 and applies identically to D3D12 / Vulkan compute on AMD, NVIDIA, and Intel hardware.

## Examples

### Bad

```hlsl
// 32 rows, 32 columns. Reading column j across rows i = 0..31 forces every
// lane in the wave onto bank ((i*32 + j) mod 32) = (j mod 32) — one bank,
// 32-way conflict, 32x slowdown.
groupshared float g_Tile[32][32];

[numthreads(32, 1, 1)]
void cs_transpose(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g_Tile[gi][dtid.x] = SrcBuffer[dtid.y * 32 + dtid.x];
    GroupMemoryBarrierWithGroupSync();
    // Read down a column — all 32 lanes target one bank.
    DstBuffer[dtid.x * 32 + dtid.y] = g_Tile[dtid.x][gi];
}
```

### Good

```hlsl
// +1 padding on the inner dimension distributes column accesses across
// all 32 banks. 33 mod 32 = 1, so consecutive rows step through banks
// consecutively rather than colliding.
groupshared float g_Tile[32][33];

[numthreads(32, 1, 1)]
void cs_transpose_padded(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g_Tile[gi][dtid.x] = SrcBuffer[dtid.y * 32 + dtid.x];
    GroupMemoryBarrierWithGroupSync();
    DstBuffer[dtid.x * 32 + dtid.y] = g_Tile[dtid.x][gi];
}
```

## Options

- `bank-count` (integer, default: 32) — the assumed LDS bank count. Set to 16 if targeting Intel Xe-HPG-style SLM where the bank count is half.

## Fix availability

**suggestion** — The fix changes the declared size of a groupshared array, which alters LDS budget and may affect occupancy bookkeeping that the shader author tracks elsewhere. The diagnostic identifies the array declaration and the offending index expression and proposes the +1 padding form.

## See also

- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — barrier-omission hazards in groupshared memory
- Related rule: [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md) — atomic contention on bins
- NVIDIA CUDA C++ Best Practices Guide: shared memory bank conflicts (the canonical reference)
- AMD GPUOpen: RDNA performance guide — LDS section
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-stride-32-bank-conflict.md)

*© 2026 NelCit, CC-BY-4.0.*
