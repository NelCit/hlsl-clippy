// Phase 4 — loop invariance / redundant computation rules (extra patterns).
// Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

Texture2D    BlurTex  : register(t0);
Texture2D    NoiseTex : register(t1);
SamplerState Linear   : register(s0);

RWTexture2D<float4> BlurOut : register(u0);

cbuffer BlurCB {
    float2 Direction;   // (1,0) or (0,1) for separable blur
    float  Sigma;       // Gaussian sigma — loop-invariant
    float  Exposure;
    uint   TapCount;    // number of blur taps — loop-invariant
    float  NearZ;
    float2 InvRes;
};

groupshared float4 GBlur[128];

// --- loop-invariant-sample (additional pattern) ---

// HIT(loop-invariant-sample): center_uv never changes inside the loop — the
// Sample result is constant and should be computed once before the loop.
float4 blur_with_invariant_center(float2 uv) {
    float2 center_uv = uv;
    float4 acc = 0;
    float  wsum = 0;
    for (uint i = 0; i < TapCount; ++i) {
        float  w = exp(-(float)i * (float)i / (2.0 * Sigma * Sigma));
        float2 offset = Direction * (float)i * InvRes;
        acc  += BlurTex.SampleLevel(Linear, uv + offset, 0) * w;
        // HIT(loop-invariant-sample): same invariant UV used again inside loop.
        acc  += NoiseTex.SampleLevel(Linear, center_uv, 0) * (w * 0.01);
        wsum += w;
    }
    return acc / wsum;
}

// SHOULD-NOT-HIT(loop-invariant-sample): UV varies with loop counter — not invariant.
float4 blur_varying_uv(float2 uv) {
    float4 acc = 0;
    for (uint i = 0; i < TapCount; ++i) {
        float2 offset = Direction * (float)i * InvRes;
        acc += BlurTex.SampleLevel(Linear, uv + offset, 0);
    }
    return acc / (float)TapCount;
}

// --- cbuffer-load-in-loop (additional pattern) ---

[numthreads(128, 1, 1)]
void cs_gaussian_blur(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    float2 uv = ((float2)dtid.xy + 0.5) * InvRes;

    // Tile load into LDS.
    GBlur[gi] = BlurTex.SampleLevel(Linear, uv, 0);
    GroupMemoryBarrierWithGroupSync();

    float4 acc  = 0;
    float  wsum = 0;
    for (uint i = 0; i < TapCount; ++i) {
        // HIT(cbuffer-load-in-loop): Sigma reloaded from cbuffer every iteration;
        // hoist `Sigma * Sigma` into a local before the loop.
        float g = exp(-(float)i * (float)i / (2.0 * Sigma * Sigma));
        // HIT(cbuffer-load-in-loop): NearZ also reloaded every iteration;
        // loop-invariant — compute outside.
        float linearDepth = NearZ / max((float)i + 1.0, NearZ);
        uint  src = min(gi + i, 127u);
        acc  += GBlur[src] * g * linearDepth;
        wsum += g;
    }
    BlurOut[dtid.xy] = acc / max(wsum, 0.0001);
}

// --- redundant-computation-in-branch (additional patterns) ---

float4 ps_taa_resolve(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 current  = BlurTex.SampleLevel(Linear, uv, 0);
    float4 history  = NoiseTex.SampleLevel(Linear, uv, 0);
    float4 blended;
    if (current.a > 0.5) {
        // HIT(redundant-computation-in-branch): dot(current.rgb, float3(0.299,...))
        // identical in both branches — hoist luma computation.
        float luma = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
        blended = lerp(history, current, 0.1 + luma * 0.05);
    } else {
        float luma = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
        blended = lerp(history, current, 0.9 - luma * 0.05);
    }
    return blended * Exposure;
}

// SHOULD-NOT-HIT(redundant-computation-in-branch): the two branches compute
// genuinely different dot-product arguments.
float4 ps_no_redundant(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 current = BlurTex.SampleLevel(Linear, uv, 0);
    float4 history = NoiseTex.SampleLevel(Linear, uv, 0);
    float4 result;
    if (current.a > 0.5) {
        float lumaA = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
        result = current * lumaA;
    } else {
        float lumaB = dot(history.rgb, float3(0.2126, 0.7152, 0.0722));
        result = history * lumaB;
    }
    return result * Exposure;
}

// --- groupshared-uninitialized-read (additional) ---

groupshared float4 GUninit[128];

[numthreads(128, 1, 1)]
void cs_uninit_read(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    GroupMemoryBarrierWithGroupSync();
    // HIT(groupshared-uninitialized-read): GUninit[gi] is read before any thread
    // has written to it in this dispatch — undefined value.
    float4 v = GUninit[gi];
    GUninit[gi] = v + BlurTex.SampleLevel(Linear, (float2)dtid.xy * InvRes, 0);
    GroupMemoryBarrierWithGroupSync();
    BlurOut[dtid.xy] = GUninit[gi];
}
