// Phase 7 — VRS / early-z / sampler feedback rules. Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

Texture2D          AlbedoTex        : register(t0);
Texture2D          RoughnessTex     : register(t1);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP>  FeedbackMap : register(u0);
RasterizerOrderedTexture2D<float4>           ROV         : register(u1);
SamplerState       LinearSS         : register(s0);

cbuffer VrsCB {
    float  AlphaRef;
    float  DepthBias;
    uint   ShadingRate;   // DXGI_SAMPLE_DESC shading rate hint
    float  Exposure;
};

// --- vrs-incompatible-output ---

// HIT(vrs-incompatible-output): PS writes SV_Depth — this disables per-draw /
// per-primitive variable rate shading (VRS) silently, forcing full-rate.
float4 ps_writes_sv_depth(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                          out float svDepth : SV_Depth) : SV_Target {
    float4 col = AlbedoTex.Sample(LinearSS, uv);
    svDepth    = pos.z + DepthBias;
    return col * Exposure;
}

// SHOULD-NOT-HIT(vrs-incompatible-output): no SV_Depth output; VRS can apply.
float4 ps_no_sv_depth(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return AlbedoTex.Sample(LinearSS, uv) * Exposure;
}

// --- early-z-disabled-by-conditional-discard ---

// HIT(early-z-disabled-by-conditional-discard): discard inside a non-uniform
// branch (depends on per-pixel alpha) without [earlydepthstencil] — HW disables
// early depth test for the entire draw; depth writes also become late.
float4 ps_conditional_discard(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 col = AlbedoTex.Sample(LinearSS, uv);
    if (col.a < AlphaRef) discard;
    return col * Exposure;
}

// SHOULD-NOT-HIT(early-z-disabled-by-conditional-discard): [earlydepthstencil]
// acknowledges the interaction; depth tested early even with potential discard.
[earlydepthstencil]
float4 ps_earlydepth_discard(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 col = AlbedoTex.Sample(LinearSS, uv);
    if (col.a < AlphaRef) discard;
    return col * Exposure;
}

// --- sv-depth-vs-conservative-depth ---

// HIT(sv-depth-vs-conservative-depth): PS writes SV_Depth where the value is
// always >= the rasterised depth (adding a positive bias only) — use
// SV_DepthGreaterEqual to keep early-Z working.
float4 ps_nonconservative_depth(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                                out float outDepth : SV_Depth) : SV_Target {
    float4 r = RoughnessTex.Sample(LinearSS, uv);
    outDepth = pos.z + abs(DepthBias);   // always >= rasterised depth
    return r * Exposure;
}

// SHOULD-NOT-HIT(sv-depth-vs-conservative-depth): SV_DepthGreaterEqual already used.
float4 ps_conservative_depth(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                              out float outDepth : SV_DepthGreaterEqual) : SV_Target {
    float4 r = RoughnessTex.Sample(LinearSS, uv);
    outDepth = pos.z + abs(DepthBias);
    return r * Exposure;
}

// --- rov-without-earlydepthstencil ---

// HIT(rov-without-earlydepthstencil): RasterizerOrderedTexture2D used without
// [earlydepthstencil] and without depth/discard hazards — conservative depth test
// serialises the ROV writes without benefit; add [earlydepthstencil].
float4 ps_rov_no_early_depth(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 existing = ROV[(uint2)pos.xy];
    float4 src      = AlbedoTex.Sample(LinearSS, uv);
    float4 blended  = lerp(existing, src, src.a);
    ROV[(uint2)pos.xy] = blended;
    return blended * Exposure;
}

// --- feedback-write-wrong-stage ---

// HIT(feedback-write-wrong-stage): WriteSamplerFeedback in a vertex shader
// (only PS is spec-allowed for sampler feedback writes on D3D12 / SM 6.5+).
float4 vs_feedback_wrong_stage(float3 pos : POSITION, float2 uv : TEXCOORD0) : SV_Position {
    // HIT(feedback-write-wrong-stage): spec restricts sampler feedback writes to PS.
    FeedbackMap.WriteSamplerFeedback(AlbedoTex, LinearSS, uv);
    return float4(pos, 1.0);
}

// --- feedback-every-sample ---

// HIT(feedback-every-sample): WriteSamplerFeedback on every sample in the PS hot
// path — spec recommends stochastic gating (discard ~99% of writes) to reduce
// driver/hardware overhead.
float4 ps_feedback_every_sample(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 col = AlbedoTex.Sample(LinearSS, uv);
    // HIT(feedback-every-sample): unconditional write every invocation; gate with
    // stochastic check e.g. (dtid.x & 127u) == 0u.
    FeedbackMap.WriteSamplerFeedback(AlbedoTex, LinearSS, uv);
    return col * Exposure;
}

// SHOULD-NOT-HIT(feedback-every-sample): gated on a stochastic hash — ~1/128 rate.
float4 ps_feedback_stochastic(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 col = AlbedoTex.Sample(LinearSS, uv);
    uint2  coord = (uint2)pos.xy;
    if (((coord.x ^ coord.y) & 127u) == 0u) {
        FeedbackMap.WriteSamplerFeedback(AlbedoTex, LinearSS, uv);
    }
    return col * Exposure;
}
