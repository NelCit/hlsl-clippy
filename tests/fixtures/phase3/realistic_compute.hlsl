// Phase 3 — realistic deferred tile-shading compute shader with deliberate rule firings.
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.

// G-Buffer inputs.
Texture2D<float4>  GBufAlbedo   : register(t0);
Texture2D<float2>  GBufNormal   : register(t1);   // oct-encoded
Texture2D<float>   GBufDepth    : register(t2);
Texture2D<float4>  GBufORM      : register(t3);   // occlusion/roughness/metallic
Texture2D<float4>  ShadowAtlas  : register(t4);
Texture2DArray     IrradArray   : register(t5);

// Outputs.
RWTexture2D<float4> LitOutput   : register(u0);

// Only-read UAV — should be demoted to SRV.
// HIT(rwresource-read-only-usage): DebugOverlay is RW but only ever Load'd below.
RWTexture2D<float4> DebugOverlay : register(u1);

cbuffer TileCB {
    uint2  TileCount;
    float2 InvScreenSize;
    float4x4 InvViewProj;
    float3 CameraPos;
    float  NearZ;
    float  FarZ;
    uint   NumLights;
    uint   IrradSlice;      // dynamically uniform per dispatch
    float  ExposureBias;
};

// HIT(cbuffer-fits-rootconstants): DispatchCB is 8 bytes — fits in root constants.
cbuffer DispatchCB {
    uint DispatchX;
    uint DispatchY;
};

struct GpuPointLight {
    float3 pos;
    float  radius;
    float3 color;
    float  intensity;
    // 32 bytes — 2x 16-byte aligned; good stride.
};

// Stride is fine here (32 bytes) — no HIT for structured-buffer-stride-mismatch.
StructuredBuffer<GpuPointLight> Lights : register(t6);

SamplerState LinearClamp : register(s0);

groupshared uint TileLightIndices[256];
groupshared uint TileLightCount;

// Oct-decode for compact normal storage.
float3 oct_decode(float2 e) {
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float  t = saturate(-n.z);
    n.xy += n.xy >= 0 ? -t : t;
    return normalize(n);
}

// Reconstruct world position from depth and inverse VP.
float3 reconstruct_world(uint2 coord, float depth) {
    float2 ndc = ((float2)coord + 0.5) * InvScreenSize * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void cs_tile_shade(uint3 gid  : SV_GroupID,
                   uint3 dtid : SV_DispatchThreadID,
                   uint  gi   : SV_GroupIndex) {
    uint2 coord = dtid.xy;

    // Initialize groupshared tile light count on thread 0.
    if (gi == 0) TileLightCount = 0;
    GroupMemoryBarrierWithGroupSync();

    // Load G-buffer.
    float  depth   = GBufDepth.Load(int3(coord, 0)).r;
    float4 albORM  = GBufAlbedo.Load(int3(coord, 0));
    float2 encN    = GBufNormal.Load(int3(coord, 0)).rg;
    float4 orm     = GBufORM.Load(int3(coord, 0));
    float3 worldP  = reconstruct_world(coord, depth);
    float3 N       = oct_decode(encN);
    float3 V       = normalize(CameraPos - worldP);

    // Debug overlay read (only read — rule fires on the resource declaration).
    float4 dbg = DebugOverlay.Load(int3(coord, 0));

    // Tile-frustum light culling (simplified — AABB vs sphere).
    for (uint li = gi; li < NumLights; li += 64u) {
        GpuPointLight light = Lights[li];
        float3 delta = worldP - light.pos;
        if (dot(delta, delta) < light.radius * light.radius) {
            uint slot;
            InterlockedAdd(TileLightCount, 1u, slot);
            if (slot < 256u) TileLightIndices[slot] = li;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Shading loop.
    float3 radiance = 0;
    uint   count    = min(TileLightCount, 256u);
    for (uint k = 0; k < count; ++k) {
        GpuPointLight lgt = Lights[TileLightIndices[k]];
        float3 L   = normalize(lgt.pos - worldP);
        float NdL  = saturate(dot(N, L));
        float3 H   = normalize(L + V);
        float NdH  = saturate(dot(N, H));
        float rough = orm.g;
        // HIT(cbuffer-load-in-loop): ExposureBias and NearZ loaded from cbuffer
        // inside this shading loop — invariant; compute once outside.
        float evScale = ExposureBias * (1.0 / NearZ);
        radiance += lgt.color * lgt.intensity * NdL * NdH * evScale;
    }

    // HIT(texture-array-known-slice-uniform): IrradSlice is a cbuffer value —
    // dynamically uniform; could use a non-array texture for this dispatch.
    float3 irrad = IrradArray.SampleLevel(LinearClamp, float3(N.xy * 0.5 + 0.5, (float)IrradSlice), 0).rgb;

    float4 result = float4((radiance + irrad) * albORM.rgb + dbg.rgb * 0.0001, 1.0);
    LitOutput[coord] = result;
}
