// Phase 4 — interlocked / atomic rules (ADR 0007 patterns).
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

RWStructuredBuffer<uint>  LightBins    : register(u0);
RWStructuredBuffer<uint>  FloatBitsRW  : register(u1);
RWStructuredBuffer<float> FloatBufSM66 : register(u2);   // SM 6.6 float atomics

cbuffer AtomicCB {
    uint NumLights;
    uint NumBins;
    uint BinShift;    // log2(bin width)
    uint Pad;
};

groupshared uint LocalBins[64];

// --- interlocked-bin-without-wave-prereduce ---

// HIT(interlocked-bin-without-wave-prereduce): each thread independently
// InterlockedAdd-s to a small fixed set of bins with no wave-level pre-reduction.
// WaveActiveSum per bin followed by a single atomic cuts traffic 32-64x.
[numthreads(64, 1, 1)]
void cs_histogram_naive(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (dtid.x >= NumLights) return;
    uint bin = (dtid.x >> BinShift) % NumBins;
    // Direct atomic — 64 threads each hit the same small bin set.
    InterlockedAdd(LightBins[bin], 1u);
}

// SHOULD-NOT-HIT(interlocked-bin-without-wave-prereduce): WaveActiveSum
// pre-reduces within the wave before the single global atomic.
[numthreads(64, 1, 1)]
void cs_histogram_wave_prereduced(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (dtid.x >= NumLights) return;
    uint bin    = (dtid.x >> BinShift) % NumBins;
    uint isThis = (gi % NumBins) == bin ? 1u : 0u;
    uint waveSum = WaveActiveSum(isThis);
    if (WaveIsFirstLane()) {
        InterlockedAdd(LightBins[bin], waveSum);
    }
}

// --- groupshared bin pre-reduction (a correct approach mixing groupshared + LDS) ---

[numthreads(64, 1, 1)]
void cs_histogram_lds(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // Initialise LDS bins on first 64 threads (assuming NumBins <= 64).
    if (gi < NumBins) LocalBins[gi] = 0u;
    GroupMemoryBarrierWithGroupSync();

    if (dtid.x < NumLights) {
        uint bin = (dtid.x >> BinShift) % NumBins;
        InterlockedAdd(LocalBins[bin], 1u);   // local — not flagged
    }
    GroupMemoryBarrierWithGroupSync();

    if (gi < NumBins) {
        InterlockedAdd(LightBins[gi], LocalBins[gi]);  // one global atomic per bin per group
    }
}

// --- interlocked-float-bit-cast-trick ---

// HIT(interlocked-float-bit-cast-trick): hand-rolled asuint / sign-flip dance
// to perform atomic min on floats — use SM 6.6 InterlockedMin on float directly.
void atomic_float_min_manual(uint idx, float val) {
    // Standard hand-rolled trick: treat float bits as signed int for atomic min.
    uint ui = asuint(val);
    if (ui & 0x80000000u) ui = ~ui;
    else                   ui = ui ^ 0x80000000u;
    InterlockedMin(FloatBitsRW[idx], ui);
}

// SHOULD-NOT-HIT(interlocked-float-bit-cast-trick): SM 6.6 native float atomic.
void atomic_float_min_sm66(uint idx, float val) {
    InterlockedMin(FloatBufSM66[idx], val);
}

// Entry point.
[numthreads(64, 1, 1)]
void cs_atomics_entry(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (dtid.x < NumLights) {
        atomic_float_min_manual(dtid.x % NumBins, (float)dtid.x * 0.001);
        atomic_float_min_sm66(dtid.x % NumBins,   (float)dtid.x * 0.001);
    }
    cs_histogram_naive(dtid, uint3(gi, 0, 0));
}
