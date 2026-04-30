// Phase 3 — texture / sampling rules (extra patterns). Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.
// Note: cbuffer register suffix omitted per tree-sitter-hlsl v0.2.0 parsing gap.

Texture2D                 Roughness    : register(t0);
Texture2DArray            IrradianceArray : register(t1);
Texture2D                 ShadowAtlas  : register(t2);
Texture2D<float4>         ColorRT      : register(t3);
SamplerState              LinearWrap   : register(s0);
SamplerComparisonState    CmpSampler   : register(s1);

cbuffer ShadingCB {
    float2 TexelSize;
    float  ShadowBias;
    uint   ArraySlice;        // dynamically uniform per batch draw call
    float3 LightDir;
    float  LightNear;
};

// --- gather-channel-narrowing ---

float4 sample_roughness_gather(float2 uv) {
    // HIT(gather-channel-narrowing): only .g channel used → GatherGreen.
    return float4(Roughness.Gather(LinearWrap, uv).g, 0, 0, 1);
}

// SHOULD-NOT-HIT(gather-channel-narrowing): all four channels of the gather are consumed.
float4 sample_roughness_gather_all(float2 uv) {
    float4 g = Roughness.Gather(LinearWrap, uv);
    return g.xyzw;
}

// --- samplecmp-vs-manual-compare ---

float pcf_manual(float2 uv, float refDepth) {
    // HIT(samplecmp-vs-manual-compare): hand-rolled depth test; use SampleCmp
    // with CmpSampler for hardware PCF across the 2x2 footprint.
    float smpl = ShadowAtlas.Sample(LinearWrap, uv).r;
    return smpl < (refDepth - ShadowBias) ? 0.0 : 1.0;
}

// SHOULD-NOT-HIT(samplecmp-vs-manual-compare): this already uses SampleCmp correctly.
float pcf_correct(float2 uv, float refDepth) {
    return ShadowAtlas.SampleCmp(CmpSampler, uv, refDepth - ShadowBias);
}

// --- texture-array-known-slice-uniform ---

float3 irradiance_probe(float2 uv) {
    // HIT(texture-array-known-slice-uniform): ArraySlice is dynamically uniform
    // (cbuffer); accessing a single uniform slice → could demote to Texture2D.
    return IrradianceArray.Sample(LinearWrap, float3(uv, (float)ArraySlice)).rgb;
}

// SHOULD-NOT-HIT(texture-array-known-slice-uniform): slice is per-thread divergent.
float3 irradiance_per_thread(float2 uv, uint threadSlice) {
    return IrradianceArray.Sample(LinearWrap, float3(uv, (float)threadSlice)).rgb;
}

// --- sample-in-loop-implicit-grad ---

// HIT(sample-in-loop-implicit-grad): Texture2D.Sample (implicit derivatives) inside
// a loop — derivatives are undefined or UB when lane divergence changes across iterations.
float4 accumulate_samples(float2 uv, int count) {
    float4 acc = 0;
    for (int i = 0; i < count; ++i) {
        acc += ColorRT.Sample(LinearWrap, uv + float2((float)i, 0) * TexelSize.x);
    }
    return acc / (float)count;
}

// SHOULD-NOT-HIT(sample-in-loop-implicit-grad): SampleLevel avoids implicit grads.
float4 accumulate_samples_safe(float2 uv, int count) {
    float4 acc = 0;
    for (int i = 0; i < count; ++i) {
        acc += ColorRT.SampleLevel(LinearWrap, uv + float2((float)i, 0) * TexelSize.x, 0);
    }
    return acc / (float)count;
}

// Entry point.
float4 ps_textures_extra(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 roughGather = sample_roughness_gather(uv);
    float  shadow      = pcf_manual(uv, pos.z);
    float3 irr         = irradiance_probe(uv);
    float4 accumulated = accumulate_samples(uv, 4);
    return float4(irr * roughGather.r * shadow, 1.0) + accumulated * 0.01;
}
