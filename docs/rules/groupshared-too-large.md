---
id: groupshared-too-large
category: workgroup
severity: warn
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# groupshared-too-large

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

One or more `groupshared` variable declarations in a compute shader whose combined byte size across the shader's compilation unit exceeds the configured `threshold-bytes`. The default threshold is 16384 bytes (16 KB). The size is computed from the declared types: `groupshared float HugeShared[16384]` is 65536 bytes (64 KB) and fires the rule at the default threshold. Arrays of `float4` (16 bytes each) and matrices are counted accordingly. All `groupshared` declarations visible to the compilation unit are summed, not counted per-function. The rule reports the total computed size alongside the threshold. It does not fire when the total is at or below the threshold.

## Why it matters on a GPU

Groupshared memory maps directly to Local Data Store (LDS) on AMD hardware and Shared Memory on NVIDIA hardware. Both are on-die SRAM resources that are physically partitioned among the compute units (CUs) or streaming multiprocessors (SMs) running concurrently on the chip. On AMD RDNA 3, each CU has 64 KB of LDS. On NVIDIA Turing, each SM has 64 KB of configurable shared memory (the split between L1 cache and shared memory is chosen at kernel launch, with a maximum shared memory allocation of 48 KB per thread block under the default split). On Intel Xe-HPG, each Xe core has 64 KB of shared local memory per EU cluster.

The wave occupancy of a CU/SM is determined by the most constraining resource: register file usage, wave-slot budget, and LDS usage. When a shader declares 64 KB of groupshared memory, the LDS is fully consumed by a single thread group. No other thread group can be co-scheduled on the same CU/SM, and the occupancy drops from the theoretical maximum (8 waves per CU on RDNA 3 at 32 threads per wave, 4 groups of 64 threads each) to 1 wave per CU. At 1 wave occupancy, the hardware has no alternative wave to switch to while a memory operation is in flight, and every L2 cache miss stalls the entire CU until the memory request returns — typically 200-400 clock cycles on RDNA 3 and Turing. This converts a latency-hiding architecture into a latency-bound one, and observed throughput drops proportionally.

The threshold of 16 KB (default) targets a commonly-cited practical limit. At 16 KB per thread group, RDNA 3 can sustain 4 concurrent thread groups per CU (4 x 16 KB = 64 KB), allowing up to 4 x 2 = 8 waves of 64 threads per CU — the maximum wave occupancy. At 32 KB, occupancy drops to 2 thread groups, which is still viable for many algorithms but halves the latency-hiding depth. At 64 KB, occupancy is 1 and the shader is maximally LDS-bound. The rule fires at 16 KB to prompt authors to review whether the full allocation is necessary; common reducers include tiling the groupshared access to process a fraction of the data per batch, packing `float` to `float16` (halving LDS usage on SM 6.2+ targets), or restructuring the algorithm to require less cross-thread data sharing.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/workgroup.hlsl, lines 6-7
// HIT(groupshared-too-large): 64 KB groupshared blows occupancy on every vendor.
groupshared float4 SmallTile[8 * 8];   // 1 KB — fine
groupshared float  HugeShared[16384];  // 64 KB — exceeds threshold

[numthreads(8, 8, 1)]
void cs_misaligned(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}
```

### Good

```hlsl
// Tile the algorithm: process a smaller working set per group.
groupshared float4 SmallTile[8 * 8];   // 512 bytes — well within budget

[numthreads(8, 8, 1)]
void cs_tiled(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}

// Where half-precision is acceptable (SM 6.2+), pack to float16 to halve LDS.
groupshared float16_t4 HalfTile[16 * 16];  // 8 KB instead of 16 KB for float4
```

## Options

- `threshold-bytes` (integer, default: `16384`) — the maximum total groupshared byte size before the rule fires. Set to `32768` for algorithms where 32 KB is acceptable and the target hardware is AMD RDNA 3 or NVIDIA Ada Lovelace (which support higher shared-memory configurations). Set to `8192` for stricter occupancy requirements. To configure per-project:

```toml
[rules.groupshared-too-large]
threshold-bytes = 32768
```

## Fix availability

**none** — Reducing groupshared usage requires algorithmic restructuring (tiling, packing, or eliminating cross-thread data sharing), which cannot be automated. The diagnostic reports the total computed size and the threshold. Add `// hlsl-clippy: allow(groupshared-too-large)` to suppress the diagnostic when a large LDS allocation is intentional and the occupancy trade-off is accepted by the author.

## See also

- Related rule: [`numthreads-not-wave-aligned`](numthreads-not-wave-aligned.md) — thread group total not divisible by wave size
- Related rule: [`numthreads-too-small`](numthreads-too-small.md) — thread group total smaller than minimum wave size
- HLSL reference: `groupshared` storage class, LDS limits in the DirectX HLSL Shader Model 6.x documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-too-large.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
