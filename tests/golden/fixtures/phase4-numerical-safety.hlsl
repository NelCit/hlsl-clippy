// Phase 4 — numerical safety rules. Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

Texture2D    DepthTex  : register(t0);
Texture2D    NormalTex : register(t1);
SamplerState PointSS   : register(s0);

cbuffer SafetyCB {
    float3   LightDir;
    float    Bias;
    float    FarZ;
    float    Epsilon;     // caller-supplied small value
};

// --- acos-without-saturate ---

// HIT(acos-without-saturate): dot of two non-clamped vectors can exceed [-1,1]
// due to floating-point error, producing NaN from acos.
float angle_between(float3 a, float3 b) {
    return acos(dot(a, b));
}

// HIT(acos-without-saturate): dot normalised inside the expression — numerics
// can still drift past 1.0 for coincident unit vectors.
float specular_angle(float3 n, float3 h) {
    float d = dot(normalize(n), normalize(h));
    return acos(d);
}

// SHOULD-NOT-HIT(acos-without-saturate): the dot is explicitly clamped to [-1,1].
float safe_angle(float3 a, float3 b) {
    return acos(saturate(dot(a, b)));
}

// SHOULD-NOT-HIT(acos-without-saturate): clamp with explicit bounds.
float safe_angle_clamp(float3 a, float3 b) {
    return acos(clamp(dot(a, b), -1.0, 1.0));
}

// --- div-without-epsilon ---

// HIT(div-without-epsilon): dot(v, v) is zero when v == 0 — division by zero.
float3 project_onto(float3 v, float3 axis) {
    return axis * (dot(v, axis) / dot(axis, axis));
}

// HIT(div-without-epsilon): length(v) can be zero for degenerate input.
float3 manual_norm(float3 v) {
    return v / length(v);
}

// SHOULD-NOT-HIT(div-without-epsilon): max() with Epsilon guards against zero.
float3 safe_project(float3 v, float3 axis) {
    return axis * (dot(v, axis) / max(dot(axis, axis), 1e-7));
}

// SHOULD-NOT-HIT(div-without-epsilon): explicit epsilon on length denominator.
float3 safe_norm(float3 v) {
    return v / max(length(v), Epsilon);
}

// --- sqrt-of-potentially-negative ---

// HIT(sqrt-of-potentially-negative): (1 - k*k) can be negative when |k| > 1,
// which happens when refraction index ratio is out of range.
float refract_sin(float cos_theta, float eta) {
    float k = 1.0 - eta * eta * (1.0 - cos_theta * cos_theta);
    return sqrt(k);
}

// HIT(sqrt-of-potentially-negative): x - FarZ can be negative when depth > far.
float linearize_depth_raw(float depth) {
    return sqrt(depth - FarZ);
}

// SHOULD-NOT-HIT(sqrt-of-potentially-negative): max() ensures non-negative operand.
float linearize_depth_safe(float depth) {
    return sqrt(max(depth - FarZ, 0.0));
}

// SHOULD-NOT-HIT(sqrt-of-potentially-negative): abs() before sqrt is safe.
float safe_sqrt(float x) {
    return sqrt(abs(x));
}

// Entry point.
float4 ps_numerical_safety(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 n   = NormalTex.Sample(PointSS, uv).xyz * 2.0 - 1.0;
    float  ang = angle_between(n, LightDir);
    float  lin = linearize_depth_safe(DepthTex.Sample(PointSS, uv).r);
    float3 prj = safe_project(n, LightDir);
    return float4(prj * lin + ang * 0.01, 1.0);
}
