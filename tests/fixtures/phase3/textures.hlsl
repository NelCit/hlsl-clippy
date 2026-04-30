// Phase 3 — texture / sampling rules.

Texture2D                BaseColor : register(t0);
Texture2D                Normal    : register(t1);  // mipped
Texture2DArray           Stack     : register(t2);
Texture2D                ShadowMap : register(t3);
SamplerState             Bilinear  : register(s0);
SamplerComparisonState   ShadowCmp : register(s1);

cbuffer Globals : register(b0) {
    float2 InvScreen;
    uint   StackSlice;   // dynamically uniform across the dispatch
};

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    // HIT(samplelevel-with-zero-on-mipped): explicit mip 0 on a mipped texture.
    float3 n = Normal.SampleLevel(Bilinear, uv, 0).xyz * 2.0 - 1.0;

    // HIT(gather-channel-narrowing): only the .r channel is used → GatherRed.
    float r = BaseColor.Gather(Bilinear, uv).r;

    // HIT(samplegrad-with-constant-grads): zero gradients = SampleLevel(0).
    float3 c = BaseColor.SampleGrad(Bilinear, uv, float2(0, 0), float2(0, 0)).rgb;

    // HIT(samplecmp-vs-manual-compare): hand-rolled depth compare. Use SampleCmp
    // with the comparison sampler for hardware PCF.
    float refDepth = pos.z;
    float shadowSample = ShadowMap.Sample(Bilinear, uv).r;
    float shadow = shadowSample < refDepth ? 1.0 : 0.0;

    // HIT(texture-array-known-slice-uniform): slice index is dynamically uniform.
    float3 stacked = Stack.Sample(Bilinear, float3(uv, StackSlice)).rgb;

    return float4(c * r + stacked * shadow * n.z, 1.0);
}
