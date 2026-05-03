// Phase 3 — resource binding rules (extra patterns). Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.
// Note: cbuffer register suffix parsing gap in tree-sitter-hlsl v0.2.0 — cbuffers declared
// without the register suffix below to keep parsing clean where possible.

// HIT(cbuffer-fits-rootconstants): 4 DWORDs = 16 bytes, fits in root constants on D3D12.
cbuffer PushCB {
    uint  DrawId;
    uint  MeshletOffset;
    uint  InstanceCount;
    float Time;
};

// HIT(cbuffer-fits-rootconstants): only 2 DWORDs — trivially root-constant material.
cbuffer TinyBlurCB {
    float BlurRadius;
    float BlurSigma;
};

// HIT(unused-cbuffer-field): DebugChannel declared but never referenced in this file.
cbuffer DebugCB {
    float4 DebugTint;
    uint   DebugChannel;   // never read below
};

// SM 6.6 dynamic resources — non-uniform access pattern.
// HIT(descriptor-heap-no-non-uniform-marker): ResourceDescriptorHeap indexed
// by a per-lane divergent value without NonUniformResourceIndex.
Texture2D<float4> get_material_texture(uint tex_index) {
    return ResourceDescriptorHeap[tex_index];
}

// SHOULD-NOT-HIT(descriptor-heap-no-non-uniform-marker): index wrapped in
// NonUniformResourceIndex — correct pattern.
Texture2D<float4> get_material_texture_safe(uint tex_index) {
    return ResourceDescriptorHeap[NonUniformResourceIndex(tex_index)];
}

// HIT(dead-store-sv-target): first write to result is immediately overwritten
// before it can be read; the assignment at line A is dead.
float4 ps_dead_store(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 result = float4(1, 0, 0, 1);   // dead write — immediately overwritten
    result = float4(0, 1, 0, 1);          // this is the value that actually returns
    return result;
}

// HIT(rwresource-read-only-usage): AccumBuffer is an RWTexture2D but only
// ever loaded from (Load) — it should be a Texture2D SRV.
RWTexture2D<float4> AccumBuffer : register(u1);

float4 ps_read_only_rw(float4 pos : SV_Position) : SV_Target {
    uint2 coord = (uint2)pos.xy;
    float4 val = AccumBuffer.Load(int3(coord, 0));
    return val * DebugTint;
}

// HIT(structured-buffer-stride-mismatch): GpuLight is 28 bytes — not 16-aligned.
// Buffer stride rounds to 28; some APIs / drivers require 16-byte alignment for optimal paths.
struct GpuLight {
    float3 position;   // 12 bytes
    float  radius;     //  4 bytes
    float3 color;      // 12 bytes
                       // total: 28 bytes (not 16-aligned)
};
StructuredBuffer<GpuLight> LightBuffer : register(t4);

// Entry point tying all resources together.
float4 ps_bindings_extra(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                         uint matIdx : TEXCOORD1) : SV_Target {
    Texture2D<float4> mat = get_material_texture(matIdx);  // divergent index — HIT above
    float4 base   = mat.Sample((SamplerState)0, uv);
    GpuLight lgt  = LightBuffer[DrawId];
    float3   lit  = base.rgb * lgt.color * (BlurRadius + Time);
    return float4(lit, 1.0);
}
