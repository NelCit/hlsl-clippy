// Negative baseline — a realistic-looking PBR pass that should produce
// ZERO diagnostics across all phases. If the linter fires here, the rule
// has a false positive.

Texture2D    BaseColor : register(t0);
Texture2D    Normal    : register(t1);
SamplerState Aniso     : register(s0);

cbuffer Frame : register(b0) {
    float4x4 ViewProj;     // offset 0..63
    float3   CameraPos;    // offset 64..75
    float    Time;         // offset 76..79  (no padding hole)
    float3   LightDir;     // offset 80..91
    float    Exposure;     // offset 92..95  (no padding hole)
};

struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct VSOut {
    float4 svpos  : SV_Position;
    float3 wpos   : TEXCOORD0;
    float3 wnorm  : TEXCOORD1;
    float2 uv     : TEXCOORD2;
};

VSOut vs_main(VSIn vin) {
    VSOut o;
    o.svpos = mul(ViewProj, float4(vin.pos, 1.0));
    o.wpos  = vin.pos;
    o.wnorm = vin.normal;
    o.uv    = vin.uv;
    return o;
}

float3 fresnel_schlick(float cos_theta, float3 f0) {
    float one_minus = 1.0 - cos_theta;
    float one_minus_sq = one_minus * one_minus;
    return f0 + (1.0 - f0) * one_minus_sq * one_minus_sq * one_minus;
}

float4 ps_main(VSOut pin) : SV_Target {
    float3 n = normalize(pin.wnorm);
    float3 v = normalize(CameraPos - pin.wpos);
    float n_dot_v = saturate(dot(n, v));

    float3 albedo = BaseColor.Sample(Aniso, pin.uv).rgb;
    float3 nmap   = Normal.Sample(Aniso, pin.uv).xyz * 2.0 - 1.0;

    float3 f0 = float3(0.04, 0.04, 0.04);
    float3 fresnel = fresnel_schlick(n_dot_v, f0);

    float3 light_dir = normalize(-LightDir);
    float n_dot_l = saturate(dot(n, light_dir));

    float3 half_vec = normalize(light_dir + v);
    float n_dot_h = saturate(dot(n, half_vec));

    float3 diffuse  = albedo * (1.0 - fresnel) * n_dot_l;
    float3 specular = fresnel * n_dot_h;

    return float4((diffuse + specular) * Exposure, 1.0);
}

[numthreads(8, 8, 1)]
void cs_post(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // 8*8 = 64 threads — wave-aligned. No groupshared abuse, no divergent
    // barriers, no derivatives in branches.
    if (dtid.x == 0 && dtid.y == 0 && gi == 0) {
        // Trivial single-thread side-effect; no wave intrinsics here.
    }
}
