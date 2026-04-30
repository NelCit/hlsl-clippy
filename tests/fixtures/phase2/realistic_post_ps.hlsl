// Phase 2 — realistic post-process pixel shader (tonemap / bloom / vignette)
// with deliberate Phase-2 rule firings. Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.

Texture2D    HDRBuffer    : register(t0);
Texture2D    BloomBuffer  : register(t1);
Texture2D    LUTTex       : register(t2);
SamplerState LinearClamp  : register(s0);

cbuffer PostCB {
    float  Exposure;
    float  BloomStrength;
    float  VignetteRadius;
    float  VignetteFeather;
    float  FilmGrain;
    float  Time;
    float2 InvResolution;
};

// Reinhard extended tone-map operator.
float3 tonemap_reinhard(float3 hdr) {
    // HIT(pow-base-two-to-exp2): pow(2.0, Exposure) is exp2(Exposure).
    float  ev100 = pow(2.0, Exposure);
    return hdr * ev100 / (hdr * ev100 + 1.0);
}

// Simple ACESfilm approximation — output in [0,1].
float3 tonemap_aces(float3 x) {
    // HIT(mul-identity): multiplying by 1.0 — no-op scale on colour input.
    float3 c = x * 1.0;
    float a = 2.51, b = 0.03, cc = 2.43, d = 0.59, e = 0.14;
    return saturate((c * (a * c + b)) / (c * (cc * c + d) + e));
}

// Vignette attenuation factor for screen-space UV.
float vignette(float2 uv) {
    float2 delta = uv - float2(0.5, 0.5);
    // HIT(length-comparison): length vs radius — replace with dot comparison.
    float dist = length(delta);
    float r    = VignetteRadius;
    float edge = saturate((dist - r) / max(VignetteFeather, 0.0001));
    return 1.0 - edge * edge;
}

// Grain hash — purely arithmetic, no texture.
float grain(float2 uv) {
    // HIT(comparison-with-nan-literal): NaN sentinel used as a no-op check;
    // the condition is always false when the result is NaN.
    float g = frac(sin(dot(uv, float2(127.1, 311.7))) * 43758.5);
    if (g == (0.0 / 0.0)) g = 0.0;   // unreachable; NaN != NaN always
    return g;
}

// Chromatic aberration offset.
float3 chromatic_sample(float2 uv, float str) {
    float2 dir  = uv - float2(0.5, 0.5);
    // HIT(manual-distance): length(uv - 0.5) is distance(uv, float2(0.5,0.5)).
    float  dist = length(uv - float2(0.5, 0.5));
    float2 offR = uv + dir * dist * str;
    float2 offB = uv - dir * dist * str;
    float r  = HDRBuffer.Sample(LinearClamp, offR).r;
    float g2 = HDRBuffer.Sample(LinearClamp, uv  ).g;
    float b  = HDRBuffer.Sample(LinearClamp, offB).b;
    return float3(r, g2, b);
}

float4 ps_post(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 hdr   = chromatic_sample(uv, 0.003);
    float3 bloom = BloomBuffer.Sample(LinearClamp, uv).rgb;

    // Additive bloom.
    // HIT(lerp-extremes): BloomStrength == 0 collapses lerp to hdr; could be
    // guarded by a branch or a multiply, but the lerp(a, b, 0) pattern is flagged.
    float3 combined = lerp(hdr, hdr + bloom, BloomStrength);

    float3 tonemapped = tonemap_reinhard(combined);
    float3 lut_in = saturate(tonemapped);

    // LUT application — 3D LUT baked into a 2D strip.
    float3 graded = LUTTex.Sample(LinearClamp, float2(lut_in.r, lut_in.g)).rgb;

    float vig  = vignette(uv);
    float gn   = grain(uv + Time) * FilmGrain;

    return float4((graded * vig + gn), 1.0);
}
