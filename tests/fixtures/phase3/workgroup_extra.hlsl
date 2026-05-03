// Phase 3 — workgroup / groupshared rules (extra patterns and interaction tests).
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

RWTexture2D<float4> OutputTex : register(u0);

// --- groupshared-stride-32-bank-conflict ---

// 32-bank LDS: stride of 32 floats causes every thread in a warp/wave to land
// on the same bank, serialising all 32 accesses.
groupshared float BankConflict[32 * 32];

[numthreads(32, 1, 1)]
void cs_bank_conflict(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    uint lane = gi & 31u;
    // HIT(groupshared-stride-32-bank-conflict): index tid*32 + k causes all
    // threads to access the same LDS bank — add 1 to the row stride to fix.
    float v = BankConflict[lane * 32 + 0];
    BankConflict[lane * 32 + 0] = v * 2.0;
    GroupMemoryBarrierWithGroupSync();
    OutputTex[dtid.xy] = float4(v, v, v, 1.0);
}

// SHOULD-NOT-HIT(groupshared-stride-32-bank-conflict): stride is 33 (padded) —
// avoids the bank conflict intentionally.
groupshared float NoBankConflict[32 * 33];

[numthreads(32, 1, 1)]
void cs_no_bank_conflict(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    uint lane = gi & 31u;
    float v = NoBankConflict[lane * 33 + 0];
    NoBankConflict[lane * 33 + 0] = v * 2.0;
    GroupMemoryBarrierWithGroupSync();
    OutputTex[dtid.xy] = float4(v, v, v, 1.0);
}

// --- groupshared-write-then-no-barrier-read ---

groupshared float SharedResult[64];

[numthreads(64, 1, 1)]
void cs_write_no_barrier(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // Thread gi writes its slot.
    SharedResult[gi] = (float)dtid.x * 0.1;
    // HIT(groupshared-write-then-no-barrier-read): thread (gi ^ 1) reads a cell
    // written by a different thread with no barrier in between — data race / UB.
    float neighbour = SharedResult[gi ^ 1u];
    OutputTex[dtid.xy] = float4(neighbour, 0, 0, 1.0);
}

// SHOULD-NOT-HIT(groupshared-write-then-no-barrier-read): barrier is present
// between the write and the cross-thread read.
[numthreads(64, 1, 1)]
void cs_write_with_barrier(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    SharedResult[gi] = (float)dtid.x * 0.1;
    GroupMemoryBarrierWithGroupSync();
    float neighbour = SharedResult[gi ^ 1u];
    OutputTex[dtid.xy] = float4(neighbour, 0, 0, 1.0);
}

// --- interaction: numthreads rules mixing with groupshared rules ---

// numthreads-not-wave-aligned fires; SharedResult read also has a bank-conflict risk.
// HIT(numthreads-not-wave-aligned): 5 * 13 = 65, not a multiple of 32 or 64.
[numthreads(5, 13, 1)]
void cs_misaligned_and_conflict(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    uint lane = gi % 32u;
    // HIT(groupshared-stride-32-bank-conflict): stride-32 access pattern on this array.
    float v = BankConflict[lane * 32 + gi / 32u];
    OutputTex[dtid.xy] = float4(v, 0, 0, 1.0);
}
