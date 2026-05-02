// Phase 8 — v0.9 VRS + DXR + Nsight-gap fixture.

// HIT(vrs-without-perprimitive-or-screenspace-source): SV_ShadingRate without upstream.
struct PSOut {
    float4 color : SV_Target;
    uint rate : SV_ShadingRate;
};
PSOut ps_vrs_bad() {
    PSOut o;
    o.color = float4(1, 0, 0, 1);
    o.rate = 0;
    return o;
}

// HIT(ray-flag-force-opaque-with-anyhit): TraceRay FORCE_OPAQUE + anyhit defined.
struct Payload { float3 c; };
struct Attribs { float2 b; };

[shader("anyhit")]
void ah(inout Payload p, in Attribs a) {
    p.c = float3(0, 0, 0);
}

[shader("raygeneration")]
void rg_force_opaque() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, r, p);
}

// SHOULD-NOT-HIT(ray-flag-force-opaque-with-anyhit): no FORCE_OPAQUE flag.
[shader("raygeneration")]
void rg_normal() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}

// HIT(ser-coherence-hint-bits-overflow): bits 24 > cap 16.
void ser_overflow(uint hint) {
    MaybeReorderThread(hint, 24);
}

// SHOULD-NOT-HIT(ser-coherence-hint-bits-overflow): bits 8 within cap.
void ser_ok(uint hint) {
    MaybeReorderThread(hint, 8);
}

// HIT(sample-use-no-interleave): result used in next statement.
Texture2D tex;
SamplerState ss;
float4 ps_sample_bad(float2 uv : TEXCOORD) : SV_Target {
    float4 c = tex.Sample(ss, uv);
    return c * 2.0;
}
