// Phase 4 — control flow / divergence / data flow rules.

Texture2D                Tex      : register(t0);
SamplerState             Bilinear : register(s0);
RWStructuredBuffer<uint> Counter  : register(u0);

groupshared float Sum;

cbuffer CB : register(b0) {
    float Threshold;     // dynamically uniform across the dispatch
    uint  Mode;          // dynamically uniform
};

float4 ps_divergent_sample(float4 pos    : SV_Position,
                           float2 uv     : TEXCOORD0,
                           nointerpolation float mask : TEXCOORD1) : SV_Target {
    if (mask > 0.5) {
        // HIT(derivative-in-divergent-cf): implicit-gradient Sample inside
        // a non-uniform branch — derivatives are undefined for masked-out lanes.
        return Tex.Sample(Bilinear, uv);
    }
    return float4(0, 0, 0, 1);
}

[numthreads(64, 1, 1)]
void cs_barrier_in_branch(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x < 32) {
        // HIT(barrier-in-divergent-cf): UB — only some lanes hit the barrier.
        GroupMemoryBarrierWithGroupSync();
        Sum = (float)dtid.x;
    }
}

[numthreads(64, 1, 1)]
void cs_wave_op_divergent(uint3 dtid : SV_DispatchThreadID) {
    if ((dtid.x & 1u) != 0u) {
        // HIT(wave-intrinsic-non-uniform): WaveActiveSum requires uniform
        // entry; participating lanes differ here.
        uint s = WaveActiveSum(dtid.x);
        Counter[0] = s;
    }
}

float4 ps_discard_then_work(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    if (uv.x < 0.0) discard;
    // HIT(discard-then-work): heavy work after discard runs on helper lanes
    // for derivative computation; either reorder or annotate.
    float4 result = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        result += Tex.Sample(Bilinear, uv + (float)i * 0.001);
    }
    return result / 16.0;
}

float4 ps_branch_missing_attribute(float4 pos : SV_Position) : SV_Target {
    // HIT(branch-on-uniform-missing-attribute): Mode is uniform; tagging
    // [branch] tells the compiler to emit a real branch instead of predication.
    if (Mode == 0) {
        return float4(1, 0, 0, 1);
    } else if (Mode == 1) {
        return float4(0, 1, 0, 1);
    }
    return float4(0, 0, 1, 1);
}

float4 ps_small_loop_no_unroll(float2 uv : TEXCOORD0) : SV_Target {
    float4 sum = 0;
    // HIT(small-loop-no-unroll): 4 iterations, constant-bounded, not unrolled.
    for (int i = 0; i < 4; ++i) {
        sum += Tex.Sample(Bilinear, uv + (float)i * 0.01);
    }
    return sum;
}

float4 ps_acos_unsafe(float3 a, float3 b) : SV_Target {
    // HIT(acos-without-saturate): dot of unit vectors can drift outside [-1, 1]
    // and produce NaN through acos.
    float angle = acos(dot(a, b));
    return float4(angle, 0, 0, 1);
}

float ps_div_no_epsilon(float3 v) : SV_Target {
    // HIT(div-without-epsilon): dot(v, v) can be zero for degenerate input.
    return 1.0 / dot(v, v);
}

float ps_sqrt_negative(float x) : SV_Target {
    // HIT(sqrt-of-potentially-negative): signed input to sqrt — guard or use abs.
    return sqrt(x - 1.0);
}
