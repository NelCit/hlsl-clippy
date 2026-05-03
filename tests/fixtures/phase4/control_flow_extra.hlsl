// Phase 4 — control flow / divergence rules (extra patterns and higher realism).
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

Texture2D    ColorTex  : register(t0);
Texture2D    NoiseTex  : register(t1);
SamplerState BilinSS   : register(s0);

RWStructuredBuffer<uint> HitCounts : register(u0);

cbuffer ControlCB {
    uint   Mode;         // dynamically uniform — same for all threads in dispatch
    float  Threshold;    // dynamically uniform
    float  Time;
    float  Exposure;
};

groupshared float LDSTile[64];

// --- branch-on-uniform-missing-attribute (additional patterns) ---

float4 ps_tonemap_mode(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 hdr = ColorTex.Sample(BilinSS, uv);
    float3 col;
    // HIT(branch-on-uniform-missing-attribute): Mode is uniform across all lanes;
    // without [branch] the compiler may predicate both arms on every thread.
    if (Mode == 0u) {
        col = hdr.rgb * Exposure;
    } else if (Mode == 1u) {
        col = hdr.rgb / (hdr.rgb + 1.0);
    } else {
        col = pow(hdr.rgb, 1.0 / 2.2);
    }
    return float4(col, hdr.a);
}

// SHOULD-NOT-HIT(branch-on-uniform-missing-attribute): [branch] is already present.
float4 ps_tonemap_mode_ok(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 hdr = ColorTex.Sample(BilinSS, uv);
    float3 col;
    [branch] if (Mode == 0u) {
        col = hdr.rgb * Exposure;
    } else {
        col = hdr.rgb / (hdr.rgb + 1.0);
    }
    return float4(col, hdr.a);
}

// --- small-loop-no-unroll (additional patterns) ---

float4 ps_box_blur_3(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 acc = 0;
    // HIT(small-loop-no-unroll): 3 iterations, constant-bounded, not unrolled.
    for (int i = -1; i <= 1; ++i) {
        acc += ColorTex.Sample(BilinSS, uv + float2((float)i * 0.001, 0));
    }
    return acc / 3.0;
}

// SHOULD-NOT-HIT(small-loop-no-unroll): [unroll] is explicitly requested.
float4 ps_box_blur_3_unrolled(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 acc = 0;
    [unroll] for (int i = -1; i <= 1; ++i) {
        acc += ColorTex.Sample(BilinSS, uv + float2((float)i * 0.001, 0));
    }
    return acc / 3.0;
}

// --- discard-then-work (additional) ---

float4 ps_alpha_test_work(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 tex = ColorTex.Sample(BilinSS, uv);
    if (tex.a < 0.1) discard;
    // HIT(discard-then-work): Sample after discard runs on helper lanes; heavy
    // texture work here stresses derivative computation on masked pixels.
    float4 detail = NoiseTex.Sample(BilinSS, uv * 8.0);
    float4 overlay = ColorTex.Sample(BilinSS, uv * 2.0 + Time * 0.01);
    return tex * detail + overlay * (1.0 - tex.a);
}

// --- wave-intrinsic-non-uniform (additional patterns) ---

[numthreads(64, 1, 1)]
void cs_wave_divergent_ballot(uint3 dtid : SV_DispatchThreadID) {
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    if (noise > Threshold) {
        // HIT(wave-intrinsic-non-uniform): WavePrefixSum in a data-dependent
        // branch — participating lane set is non-uniform.
        uint prefix = WavePrefixSum(dtid.x);
        HitCounts[prefix & 0x3Fu] = dtid.x;
    }
}

// --- derivative-in-divergent-cf (additional pattern) ---

float4 ps_deriv_in_mode_branch(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                               float mask : TEXCOORD1) : SV_Target {
    float3 col = 0;
    if (mask > 0.0) {
        // HIT(derivative-in-divergent-cf): Sample with implicit gradients inside
        // a branch driven by a per-pixel value — derivatives undefined on masked lanes.
        col = NoiseTex.Sample(BilinSS, uv).rgb;
    }
    return float4(col, 1.0);
}

// --- barrier-in-divergent-cf (additional pattern) ---

[numthreads(64, 1, 1)]
void cs_nested_divergent_barrier(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    LDSTile[gi] = (float)dtid.x;
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    if (noise > Threshold) {
        // HIT(barrier-in-divergent-cf): GroupMemoryBarrier inside a divergent
        // branch — only some lanes reach this barrier, causing GPU hang / UB.
        GroupMemoryBarrierWithGroupSync();
        LDSTile[gi] *= 2.0;
    }
}
