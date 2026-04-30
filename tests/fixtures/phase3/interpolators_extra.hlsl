// Phase 3 — interpolator / semantic rules (extra patterns). Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

// --- excess-interpolators ---

// HIT(excess-interpolators): 11 TEXCOORDs (0-10) — exceeds the 10-slot effective budget
// for older D3D11 class hardware and wastes parameter cache in modern GPUs.
struct VSOutFull {
    float4 svPos        : SV_Position;
    float3 worldPos     : TEXCOORD0;
    float3 worldNormal  : TEXCOORD1;
    float3 worldTangent : TEXCOORD2;
    float3 worldBiTan   : TEXCOORD3;
    float2 uv0          : TEXCOORD4;
    float2 uv1          : TEXCOORD5;
    float2 uv2          : TEXCOORD6;
    float4 prevClip     : TEXCOORD7;
    float4 currClip     : TEXCOORD8;
    float  fogFactor    : TEXCOORD9;
    float  ao           : TEXCOORD10;  // 11th TEXCOORD — pushes over budget
};

// SHOULD-NOT-HIT(excess-interpolators): only 5 TEXCOORDs — well within budget.
struct VSOutLean {
    float4 svPos   : SV_Position;
    float3 wPos    : TEXCOORD0;
    float3 wNorm   : TEXCOORD1;
    float2 uv      : TEXCOORD2;
    float4 lsPos   : TEXCOORD3;
};

// --- nointerpolation-mismatch ---

// Vertex side emits matIndex without nointerpolation.
struct VSOutMat {
    float4 svPos    : SV_Position;
    float2 uv       : TEXCOORD0;
    uint   matIndex : TEXCOORD1;       // no nointerpolation here
};

VSOutMat vs_with_mat(float3 pos : POSITION, float2 uv : TEXCOORD0, uint iid : SV_InstanceID) {
    VSOutMat o;
    o.svPos    = float4(pos, 1.0);
    o.uv       = uv;
    o.matIndex = iid;
    return o;
}

// HIT(nointerpolation-mismatch): pixel shader declares matIndex as nointerpolation
// but the vertex shader output struct does not — the qualifier should match.
float4 ps_flat_mat(float4 pos    : SV_Position,
                   float2 uv     : TEXCOORD0,
                   nointerpolation uint matIndex : TEXCOORD1) : SV_Target {
    return float4((float)matIndex / 255.0, uv, 1.0);
}

// SHOULD-NOT-HIT(nointerpolation-mismatch): both VS and PS agree on nointerpolation.
struct VSOutFlat {
    float4 svPos : SV_Position;
    nointerpolation uint instId : TEXCOORD0;
};

VSOutFlat vs_flat(float3 pos : POSITION, uint iid : SV_InstanceID) {
    VSOutFlat o;
    o.svPos  = float4(pos, 1.0);
    o.instId = iid;
    return o;
}

float4 ps_flat_ok(float4 pos : SV_Position, nointerpolation uint instId : TEXCOORD0) : SV_Target {
    return float4((float)instId, 0, 0, 1);
}

// --- missing-precise-on-pcf ---

// HIT(missing-precise-on-pcf): depth arithmetic without `precise`; FMA reordering
// can introduce shadow acne. Annotate with `precise` or restructure.
float shadow_test(float shadowDepth, float sceneDepth, float bias) {
    float delta = shadowDepth - (sceneDepth + bias);
    return saturate(delta * 1000.0);
}

// SHOULD-NOT-HIT(missing-precise-on-pcf): `precise` qualifier prevents reordering.
precise float shadow_test_safe(float shadowDepth, float sceneDepth, float bias) {
    precise float delta = shadowDepth - (sceneDepth + bias);
    return saturate(delta * 1000.0);
}

// Entry point.
float4 ps_entry(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float shadow = shadow_test(pos.z, 0.5, 0.0005);
    return float4(uv, shadow, 1.0);
}
