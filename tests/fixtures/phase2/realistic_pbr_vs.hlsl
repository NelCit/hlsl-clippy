// Phase 2 — realistic PBR vertex + geometry prep shader with deliberate rule firings.
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.

struct VertexIn {
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float4 tangent   : TANGENT;    // .w = bitangent sign
    float2 uv0       : TEXCOORD0;
    float2 uv1       : TEXCOORD1;
    uint   instanceId : SV_InstanceID;
};

struct VertexOut {
    float4 svPos     : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float3 worldTan  : TEXCOORD2;
    float3 worldBiTan: TEXCOORD3;
    float2 uv0       : TEXCOORD4;
    float2 uv1       : TEXCOORD5;
    float  fogFactor : TEXCOORD6;
};

cbuffer PerFrame {
    float4x4 ViewProj;
    float3   CameraWorldPos;
    float    FogDensity;
    float3   FogColor;
    float    Time;
};

cbuffer PerObject {
    float4x4 World;
    float4x4 WorldInvTranspose;
    uint     MaterialIndex;
    uint     Pad0;
    uint     Pad1;
    uint     Pad2;
};

StructuredBuffer<float4x4> InstanceTransforms : register(t0);

// Computes a per-vertex wind displacement used in foliage rendering.
float3 compute_wind_offset(float3 wpos, float time) {
    float phase = wpos.x + wpos.z;
    // HIT(sin-cos-pair): separate sin and cos of the same expression — use sincos().
    float s = sin(phase + time);
    float c = cos(phase + time);
    return float3(s * 0.02, 0.0, c * 0.015);
}

// Exponential height fog attenuation.
float compute_fog(float3 wpos) {
    float dist = length(wpos - CameraWorldPos);
    // HIT(inv-sqrt-to-rsqrt): 1.0 / sqrt(dot) avoidable with rsqrt.
    float invDist = 1.0 / sqrt(max(dot(wpos, wpos), 0.0001));
    return saturate(1.0 - exp(-FogDensity * dist * invDist));
}

// Schlick-GGX geometry term helper shared between VS tangent-space preparation.
float g_schlick(float n_dot_v, float roughness) {
    float k = roughness * roughness * 0.5;
    // HIT(manual-mad-decomposition): n_dot_v*k separated from the denominator add,
    // preventing the compiler from folding into FMA.
    float num = n_dot_v * (1.0 - k);
    return num / (num + k);
}

VertexOut vs_main(VertexIn vin) {
    VertexOut vout;

    // Instance transform lookup.
    float4x4 inst = InstanceTransforms[vin.instanceId];
    float4x4 world = mul(inst, World);

    float3 wpos = mul(world, float4(vin.position, 1.0)).xyz;
    wpos += compute_wind_offset(wpos, Time);

    vout.svPos     = mul(ViewProj, float4(wpos, 1.0));
    vout.worldPos  = wpos;

    // Transform normal and tangent through the inverse-transpose.
    float3x3 wit = (float3x3)WorldInvTranspose;
    vout.worldNorm  = normalize(mul(wit, vin.normal));
    vout.worldTan   = normalize(mul(wit, vin.tangent.xyz));

    // HIT(cross-with-up-vector): cross with world-up (0,1,0) simplifies to
    // negations and swizzles; the built-in pattern is avoidable with a formula.
    float3 biTan    = cross(vout.worldNorm, float3(0, 1, 0)) * vin.tangent.w;
    vout.worldBiTan = normalize(biTan);

    vout.uv0       = vin.uv0;
    vout.uv1       = vin.uv1;
    vout.fogFactor = compute_fog(wpos);

    return vout;
}

// Pixel stub so the file has a complete entry point pair.
float4 ps_main(VertexOut pin) : SV_Target {
    return float4(pin.worldNorm * 0.5 + 0.5, 1.0);
}
