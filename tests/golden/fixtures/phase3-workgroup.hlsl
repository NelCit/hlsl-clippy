// Phase 3 — workgroup / threadgroup rules.

RWTexture2D<float4> Output : register(u0);

groupshared float4 SmallTile[8 * 8];      // 1 KB — fine
// HIT(groupshared-too-large): 64 KB groupshared blows occupancy on every vendor.
groupshared float  HugeShared[16384];

// HIT(numthreads-not-wave-aligned): 7 * 7 = 49, not a multiple of 32 (NV) or 64 (AMD).
[numthreads(7, 7, 1)]
void cs_misaligned(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}

// HIT(numthreads-too-small): 4 * 4 = 16, smaller than minimum wave size of 32.
[numthreads(4, 4, 1)]
void cs_too_small(uint3 dtid : SV_DispatchThreadID) {
    Output[dtid.xy] = float4(1.0, 0.0, 0.0, 1.0);
}

// Negative case: 8 * 8 = 64 — wave-aligned for AMD, integer multiple of 32 for NV.
[numthreads(8, 8, 1)]
void cs_clean(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SmallTile[gi] = float4(dtid * 0.1, 1.0);
    GroupMemoryBarrierWithGroupSync();
    Output[dtid.xy] = SmallTile[gi];
}
