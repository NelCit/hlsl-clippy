// Phase 2 — additional math simplification rules. Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

Texture2D    EnvMap   : register(t0);
SamplerState Trilinear : register(s0);

cbuffer LightingCB {
    float4x4 InvViewProj;
    float3   SunDirection;
    float    SunIntensity;
    float    RoughnessScale;
    float    ExposureBias;
};

// --- pow-base-two-to-exp2 (additional patterns) ---

float mip_level_from_roughness(float roughness) {
    // HIT(pow-base-two-to-exp2): pow(2.0, x) is exp2(x).
    float numMips = 8.0;
    return pow(2.0, roughness * numMips);
}

// SHOULD-NOT-HIT(pow-base-two-to-exp2): base is 2.0f but exponent is a literal int — pow(2,n) with int literal stays.
float integer_shift_analog(float x) {
    return pow(2.0, 3.0);  // constant fold expected; not the same pattern
}

// --- pow-integer-decomposition ---

float phong_specular(float n_dot_h, float shininess) {
    // HIT(pow-integer-decomposition): pow(x, 7.0) → decompose by squaring.
    return pow(n_dot_h, 7.0);
}

float beckmann_denom(float roughness) {
    // HIT(pow-integer-decomposition): pow(x, 4.0) → (x*x)*(x*x).
    return pow(roughness, 4.0);
}

// --- inv-sqrt-to-rsqrt (additional) ---

float3 normalize_manual(float3 v) {
    // HIT(inv-sqrt-to-rsqrt): 1.0 / sqrt(dot) is rsqrt(dot).
    float len = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return v * (1.0 / len);
}

// SHOULD-NOT-HIT(inv-sqrt-to-rsqrt): this is a division by a non-sqrt expression.
float not_rsqrt(float x) {
    return 1.0 / (x + 0.001);
}

// --- length-comparison (additional patterns) ---

bool shadow_in_range(float3 delta, float range) {
    // HIT(length-comparison): length vs constant → dot comparison avoids sqrt.
    return length(delta) <= range;
}

// SHOULD-NOT-HIT(length-comparison): length used in arithmetic, not comparison.
float attenuation(float3 d) {
    return 1.0 / (1.0 + length(d) * length(d));
}

// --- manual-mad-decomposition ---

float3 brdf_combine(float3 diffuse, float3 specular, float3 ambient, float ao) {
    // HIT(manual-mad-decomposition): (diffuse * ao) + ambient split across
    // two statements prevents the compiler from folding into a single FMA.
    float3 scaled = diffuse * ao;
    float3 result = scaled + ambient;
    return result + specular;
}

// --- dot-on-axis-aligned-vector ---

float3 extract_x_component(float3 v) {
    // HIT(dot-on-axis-aligned-vector): dot(v, (1,0,0)) is v.x.
    return float3(dot(v, float3(1, 0, 0)), dot(v, float3(0, 1, 0)), dot(v, float3(0, 0, 1)));
}

// SHOULD-NOT-HIT(dot-on-axis-aligned-vector): general dot, not axis-aligned.
float general_dot(float3 a, float3 b) {
    return dot(a, b);
}

// --- length-then-divide (normalize via division) ---

float3 incident_to_sphere(float3 v) {
    // HIT(length-then-divide): v / length(v) is normalize(v), which uses rsqrt.
    return v / length(v);
}

// --- cross-with-up-vector ---

float3 build_tangent(float3 forward) {
    // HIT(cross-with-up-vector): cross with world-up simplifies to two negations.
    return cross(forward, float3(0, 1, 0));
}

// SHOULD-NOT-HIT(cross-with-up-vector): general cross with a non-axis vector.
float3 general_cross(float3 a, float3 b) {
    return cross(a, b);
}

// --- countbits-vs-manual-popcount ---

uint count_set_bits(uint mask) {
    // HIT(countbits-vs-manual-popcount): hand-rolled popcount → countbits(mask).
    uint count = 0;
    while (mask) { count += mask & 1u; mask >>= 1u; }
    return count;
}

// --- firstbit-vs-log2-trick ---

uint msb_position(uint x) {
    // HIT(firstbit-vs-log2-trick): log2 for MSB lookup → firstbithigh(x).
    return (uint)log2((float)x);
}

// --- entry point ---

float4 ps_extra_math(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 dir = normalize(float3(uv * 2.0 - 1.0, 1.0));
    float  mip = mip_level_from_roughness(RoughnessScale);
    float3 env = EnvMap.SampleLevel(Trilinear, uv, mip).rgb;
    float3 tan = build_tangent(SunDirection);
    float3 inc = incident_to_sphere(dir);
    float  spec = phong_specular(saturate(dot(inc, SunDirection)), 7.0);
    float3 lit  = env * spec * SunIntensity * ExposureBias;
    return float4(lit, 1.0);
}
