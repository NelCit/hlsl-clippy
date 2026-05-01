// Phase 7 — ray tracing (DXR) rules. Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

// DXR built-in resources.
RaytracingAccelerationStructure Scene    : register(t0);
RWTexture2D<float4>              Output  : register(u0);

Texture2D    AlbedoTex : register(t1);
SamplerState LinearSS  : register(s0);

cbuffer RaytrCB {
    float4x4 InvViewProj;
    float3   CameraPos;
    float    TMin;
    float    TMax;
    uint     MaxDepth;
    uint     SampleCount;
    float    Exposure;
};

// --- oversized-ray-payload ---

// HIT(oversized-ray-payload): payload is 192 bytes — large payloads spill to
// the ray stack and cause heavy memory traffic on every TraceRay call.
struct BigPayload {
    float3 radiance;
    float3 throughput;
    float3 origin;
    float3 direction;
    float3 normal;
    float3 tangent;
    float4 albedo;
    float4 emission;
    float4 roughnessMetal;
    float4 prevPos;
    uint   depth;
    uint   flags;
    uint   rngState;
    float  pdf;
    // 12*4 + 12*4 + 12*4 + 12*4 + 12*4 + 12*4 = 72 floats + 4 uints = ~288B; over budget.
};

// SHOULD-NOT-HIT(oversized-ray-payload): compact payload, well under budget.
struct SmallPayload {
    float3 radiance;
    float  hitT;
    uint   flags;
};

// --- missing-accept-first-hit ---

void trace_shadow_ray(float3 origin, float3 dir, inout SmallPayload payload) {
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = TMin;
    ray.TMax      = TMax;
    // HIT(missing-accept-first-hit): shadow ray against opaque geometry without
    // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH — BVH traversal continues to far
    // geometry even though we only need the occlusion bit.
    TraceRay(Scene, RAY_FLAG_CULL_NON_OPAQUE, 0xFF, 0, 1, 0, ray, payload);
}

// SHOULD-NOT-HIT(missing-accept-first-hit): flags include ACCEPT_FIRST_HIT — correct.
void trace_shadow_ray_fast(float3 origin, float3 dir, inout SmallPayload payload) {
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = TMin;
    ray.TMax      = TMax;
    TraceRay(Scene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
             0xFF, 0, 1, 0, ray, payload);
}

// --- tracerray-conditional ---

// HIT(tracerray-conditional): TraceRay inside an if driven by a per-lane value
// (WorldRayDirection().y) — non-uniform condition extends live ranges across trace,
// causing ray stack spills.
[shader("closesthit")]
void ClosestHit(inout BigPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    float3 dir = WorldRayDirection();
    if (dir.y > 0.0) {
        RayDesc ray;
        ray.Origin    = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        ray.Direction = reflect(dir, float3(0, 1, 0));
        ray.TMin      = 0.001;
        ray.TMax      = TMax;
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    }
}

// --- anyhit-heavy-work ---

// HIT(anyhit-heavy-work): any-hit shader does a full texture sample beyond
// alpha-mask — expensive per-candidate work; move to closesthit if possible.
[shader("anyhit")]
void AnyHit(inout SmallPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    float2 uv = attr.barycentrics;
    // Alpha test is acceptable in any-hit.
    float4 albedo = AlbedoTex.SampleLevel(LinearSS, uv, 0);
    if (albedo.a < 0.5) { IgnoreHit(); return; }

    // HIT(anyhit-heavy-work): lighting computation in any-hit — this runs for
    // every BVH candidate, not just the final closest hit.
    float NdL  = saturate(dot(float3(0, 1, 0), float3(0.577, 0.577, 0.577)));
    float spec = pow(NdL, 32.0);
    payload.radiance += albedo.rgb * (NdL + spec) * Exposure;
}

// --- recursion-depth-not-declared ---

// HIT(recursion-depth-not-declared): no [MaxRecursionDepth(...)] attribute on
// the ray generation shader — driver / pipeline defaults to 1, which may be
// insufficient for path-tracing shaders with indirect bounces.
[shader("raygeneration")]
void RayGen() {
    uint2 launchIdx  = DispatchRaysIndex().xy;
    uint2 launchDims = DispatchRaysDimensions().xy;
    float2 uv = ((float2)launchIdx + 0.5) / (float2)launchDims;

    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 world = mul(InvViewProj, float4(ndc, 0.0, 1.0));
    float3 dir   = normalize(world.xyz / world.w - CameraPos);

    SmallPayload payload;
    payload.radiance = 0;
    payload.hitT     = TMax;
    payload.flags    = 0;

    trace_shadow_ray_fast(CameraPos, dir, payload);

    Output[launchIdx] = float4(payload.radiance * Exposure, 1.0);
}

[shader("miss")]
void Miss(inout SmallPayload payload) {
    payload.radiance = float3(0.2, 0.3, 0.5);  // sky colour
    payload.hitT     = TMax;
}
