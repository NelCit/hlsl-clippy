// Phase 3 — interpolator / semantic rules.

struct VSInExcess {
    float3 pos      : POSITION;
    float3 normal   : NORMAL;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float4 color    : COLOR0;
};

// HIT(excess-interpolators): output passes 9 TEXCOORD slots — exceeds typical
// interpolator budget on older HW (D3D11 era was 16, but vertex-color +
// position + 9 texcoords is wasteful).
struct VSOutExcess {
    float4 svpos   : SV_Position;
    float3 wpos    : TEXCOORD0;
    float3 wnormal : TEXCOORD1;
    float2 uv0     : TEXCOORD2;
    float2 uv1     : TEXCOORD3;
    float4 vcolor  : TEXCOORD4;
    float3 tangent : TEXCOORD5;
    float3 binorm  : TEXCOORD6;
    float4 lspos   : TEXCOORD7;
    float4 prevPos : TEXCOORD8;
    float  fog     : TEXCOORD9;
};

VSOutExcess vs_excess(VSInExcess i) {
    VSOutExcess o = (VSOutExcess)0;
    o.svpos = float4(i.pos, 1.0);
    o.wpos = i.pos;
    o.wnormal = i.normal;
    o.uv0 = i.uv0;
    o.uv1 = i.uv1;
    o.vcolor = i.color;
    return o;
}

// HIT(nointerpolation-mismatch): pixel input flagged `nointerpolation` but
// vertex output isn't — the vertex output should match.
struct VSOutFlat {
    float4 svpos    : SV_Position;
    uint   matIndex : TEXCOORD0;     // declared smooth on vertex side
};

float4 ps_flat(float4 pos : SV_Position, nointerpolation uint matIndex : TEXCOORD0) : SV_Target {
    return float4((float)matIndex, 0, 0, 1);
}

// HIT(missing-precise-on-pcf): depth-compare arithmetic without `precise`
// drifts under FMA reordering — shadow acne magnet.
float pcf_no_precise(float depth_sample, float ref_depth, float bias) {
    float diff = depth_sample - ref_depth + bias;
    return diff < 0 ? 1.0 : 0.0;
}
