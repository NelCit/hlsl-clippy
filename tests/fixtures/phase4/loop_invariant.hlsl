// Phase 4 — loop-invariance + redundant-in-branch rules.

Texture2D    Tex      : register(t0);
SamplerState Bilinear : register(s0);

cbuffer CB : register(b0) {
    float2 Center;
    float  Radius;
};

groupshared float GShared[64];

float4 ps_loop_invariant_sample(float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        // HIT(loop-invariant-sample): UV does not depend on the loop counter;
        // the sample is constant across all iterations and should be hoisted.
        acc += Tex.Sample(Bilinear, Center) * (1.0 / 16.0);
    }
    return acc;
}

float4 ps_cbuffer_load_in_loop(float2 uv : TEXCOORD0) : SV_Target {
    float r2 = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        // HIT(cbuffer-load-in-loop): Radius * Radius is loop-invariant; load
        // once into a temp before the loop.
        r2 += (Radius * Radius) * 0.125;
    }
    return float4(r2, r2, r2, 1);
}

float4 ps_redundant_in_branch(float2 uv : TEXCOORD0) : SV_Target {
    float4 base = Tex.Sample(Bilinear, uv);
    float4 result;
    if (uv.x > 0.5) {
        // HIT(redundant-computation-in-branch): pow(base.a, 5.0) computed
        // identically in both arms — hoist it out of the if.
        result = base * 2.0 + pow(base.a, 5.0);
    } else {
        result = base * 0.5 + pow(base.a, 5.0);
    }
    return result;
}

[numthreads(64, 1, 1)]
void cs_groupshared_uninit_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // No write to GShared before the read below. Race + UB.
    GroupMemoryBarrierWithGroupSync();
    // HIT(groupshared-uninitialized-read): GShared[gi] read before any thread
    // in the group has written it.
    float v = GShared[gi];
    GShared[gi] = v + (float)dtid.x;
}
