// Phase 3 — resource binding / cbuffer layout rules. Slang reflection territory.

// HIT(cbuffer-padding-hole): Time (4B) followed by LightDir (16B-aligned)
// leaves a 12-byte padding hole at offsets 4..15.
cbuffer FramePadded : register(b0) {
    float    Time;
    float3   LightDir;
    float    Exposure;
    float4x4 ViewProj;
};

// HIT(bool-straddles-16b): Tint (12B) + bool at offset 12 → bool packed
// across the 16-byte register boundary.
cbuffer StraddleCB : register(b1) {
    float3 Tint;
    bool   UseTint;
    float4 More;
};

// HIT(cbuffer-fits-rootconstants): 8 bytes total — root constants on D3D12.
cbuffer Tiny : register(b2) {
    uint InstanceID;
    uint MaterialID;
};

// HIT(oversized-cbuffer): 4 KB +; flagged at default threshold.
cbuffer Huge : register(b3) {
    float4 BigArray[256];  // 4096 bytes
    float4 Tail;
};

struct UnusedFields {
    float4 a;
    float4 b;
    // HIT(unused-cbuffer-field): `c` declared but never referenced.
    float4 c;
};
ConstantBuffer<UnusedFields> Unused : register(b4);

// HIT(structured-buffer-stride-mismatch): Particle is 12 bytes; stride
// rounds to 16 in the buffer view, wasting 25% of bandwidth.
struct Particle {
    float3 pos;
};
StructuredBuffer<Particle> Particles : register(t0);

// HIT(rwresource-read-only-usage): RW buffer that's only ever read.
RWStructuredBuffer<uint> ReadOnlyRW : register(u0);

float4 entry_main(float4 pos : SV_Position) : SV_Target {
    float3 v = Unused.a.xyz + Unused.b.xyz;
    Particle p = Particles[0];
    uint val = ReadOnlyRW[0];
    float3 lit = mul((float3x3)ViewProj, v) * Exposure + Tint;
    if (UseTint) lit *= More.rgb;
    return float4(lit + p.pos + InstanceID + MaterialID + val, 1.0);
}
