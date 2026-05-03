// Phase 4 — realistic tile-binning compute shader with deliberate uniformity /
// divergence rule mix. Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.

Texture2D<float>  DepthTex   : register(t0);
Texture2D<float4> NormalTex  : register(t1);
SamplerState      PointSS    : register(s0);

RWStructuredBuffer<uint>  TileBins    : register(u0);
RWStructuredBuffer<float> DepthMinMax : register(u1);

cbuffer BinningCB {
    uint2  TileCount;
    uint2  TileSize;
    float  Near;
    float  Far;
    uint   Mode;       // uniform: which path to run
    uint   MaxLights;
};

groupshared uint  GLocalBin[256];
groupshared float GMinDepth;
groupshared float GMaxDepth;

// Linearise hardware depth.
float linearise(float d) {
    // HIT(div-without-epsilon): Near * Far / (Far - d*Far - Near) can divide by
    // zero when d == 1 and Far == Near (degenerate projection).
    return Near * Far / (Far - d * Far - Near);
}

// Compute tile depth range using wave intrinsics — correct fast path.
void compute_tile_depth_range(uint gi, float2 uv) {
    float d   = DepthTex.SampleLevel(PointSS, uv, 0).r;
    float lin = linearise(d);

    // Wave-reduce depth bounds.
    float waveMin = WaveActiveMin(lin);
    float waveMax = WaveActiveMax(lin);

    if (WaveIsFirstLane()) {
        InterlockedMin(DepthMinMax[0], asuint(waveMin));
        InterlockedMax(DepthMinMax[1], asuint(waveMax));
    }
}

[numthreads(16, 16, 1)]
void cs_tile_bin(uint3 gid  : SV_GroupID,
                 uint3 dtid : SV_DispatchThreadID,
                 uint  gi   : SV_GroupIndex) {
    // Init shared memory.
    if (gi == 0u) {
        GMinDepth = 1e30;
        GMaxDepth = 0.0;
    }
    if (gi < 256u) GLocalBin[gi] = 0u;
    GroupMemoryBarrierWithGroupSync();

    float2 uv = (float2(dtid.xy) + 0.5) / float2(TileCount * TileSize);
    compute_tile_depth_range(gi, uv);
    GroupMemoryBarrierWithGroupSync();

    // HIT(branch-on-uniform-missing-attribute): Mode is a cbuffer value — uniform
    // across all threads; [branch] hint avoids predication overhead.
    if (Mode == 0u) {
        // Depth-only binning.
        float d   = DepthTex.SampleLevel(PointSS, uv, 0).r;
        float lin = linearise(d);
        uint  bin = (uint)((lin - Near) / (Far - Near) * 255.0) & 0xFFu;
        InterlockedAdd(GLocalBin[bin], 1u);
    } else {
        // Normal + depth binning.
        float4 n  = NormalTex.SampleLevel(PointSS, uv, 0);
        float  d  = DepthTex.SampleLevel(PointSS, uv, 0).r;
        uint   bin = (uint)(abs(n.x) * 127.0) & 0xFFu;
        InterlockedAdd(GLocalBin[bin], 1u);

        float lin = linearise(d);
        uint  dbin = (uint)((lin - Near) / (Far - Near) * 255.0) & 0xFFu;
        // HIT(interlocked-bin-without-wave-prereduce): individual InterlockedAdd
        // to small fixed bins without wave pre-reduction — 16x16=256 atomics per bin.
        InterlockedAdd(GLocalBin[dbin], 1u);
    }
    GroupMemoryBarrierWithGroupSync();

    // Flush local bins to global. One global atomic per group per bin — acceptable.
    if (gi < 256u && GLocalBin[gi] > 0u) {
        uint2 tile = gid.xy;
        uint  globalIdx = (tile.y * TileCount.x + tile.x) * 256u + gi;
        InterlockedAdd(TileBins[globalIdx], GLocalBin[gi]);
    }

    // Sample inside wave-uniform lane check — correct.
    if (WaveIsFirstLane()) {
        float centerDepth = DepthTex.SampleLevel(PointSS, float2(0.5, 0.5), 0).r;
        DepthMinMax[2] = asfloat(asuint(linearise(centerDepth)));
    }

    // HIT(wave-intrinsic-non-uniform): WaveActiveSum in a data-driven branch —
    // participating lane set depends on per-pixel depth.
    float pixelDepth = DepthTex.SampleLevel(PointSS, uv, 0).r;
    if (pixelDepth < 0.9) {
        uint sumCoord = WaveActiveSum(gi);
        GLocalBin[sumCoord & 0xFFu] += 1u;
    }
}
