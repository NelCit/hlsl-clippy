// Phase 2 — negative test cases. Patterns that LOOK like rule firings but must NOT fire.
// Hand-written fixture for shader-clippy rule validation.
// Every entry here is annotated with SHOULD-NOT-HIT explaining why it is different.

Texture2D    AlbedoTex  : register(t0);
SamplerState BilinearSS : register(s0);

cbuffer MaterialCB {
    float  Roughness;
    float  Metallic;
    float3 BaseColor;
    float  Alpha;
    float3 EmissiveColor;
    float  EmissiveScale;
};

// SHOULD-NOT-HIT(pow-to-mul): exponent 2.5 is not an integer — cannot expand to multiplies.
float pow_non_integer(float x) {
    return pow(x, 2.5);
}

// SHOULD-NOT-HIT(pow-base-two-to-exp2): base is 3.0, not 2.0.
float pow_base_three(float x) {
    return pow(3.0, x);
}

// SHOULD-NOT-HIT(pow-integer-decomposition): exponent is a runtime variable, not a literal.
float pow_runtime_exp(float x, float n) {
    return pow(x, n);
}

// SHOULD-NOT-HIT(inv-sqrt-to-rsqrt): the denominator includes an additive bias,
// so this is not 1/sqrt(x) — it is 1/(sqrt(x) + eps).
float biased_inv_sqrt(float x) {
    return 1.0 / (sqrt(x) + 0.0001);
}

// SHOULD-NOT-HIT(lerp-extremes): the weight 0.5 is not a constant 0 or 1.
float3 lerp_half(float3 a, float3 b) {
    return lerp(a, b, 0.5);
}

// SHOULD-NOT-HIT(lerp-extremes): the weight is a runtime uniform.
float3 lerp_runtime_weight(float3 a, float3 b, float t) {
    return lerp(a, b, t);
}

// SHOULD-NOT-HIT(mul-identity): the constant is 1.001, not exactly 1.0.
float3 near_identity_scale(float3 v) {
    return v * 1.001;
}

// SHOULD-NOT-HIT(redundant-saturate): inner arg is not the output of saturate;
// it is a general expression that happens to produce saturate(clamp(x,lo,hi)).
float saturate_clamp_pair(float x, float lo, float hi) {
    return saturate(clamp(x, lo, hi));
}

// SHOULD-NOT-HIT(redundant-normalize): argument is scaled, not the direct
// output of normalize, so its length may differ from 1.
float3 normalize_scaled(float3 n) {
    return normalize(n * 5.0);
}

// SHOULD-NOT-HIT(redundant-abs): abs of a dot product with two different vectors;
// the result can be negative.
float abs_of_cross_dot(float3 a, float3 b, float3 c) {
    return abs(dot(cross(a, b), c));
}

// SHOULD-NOT-HIT(length-comparison): result of length() used as an arithmetic
// operand, not as one side of a comparison.
float length_arithmetic(float3 v) {
    return length(v) * 2.0 + 1.0;
}

// SHOULD-NOT-HIT(compare-equal-float): exact comparison of two uint values
// reinterpreted as float bits — bit-exact compare is meaningful here.
bool bitpattern_equal(float a, float b) {
    return asuint(a) == asuint(b);
}

// SHOULD-NOT-HIT(comparison-with-nan-literal): isnan() is the proper test idiom,
// not a comparison against a NaN literal.
bool isnan_check(float x) {
    return isnan(x);
}

// SHOULD-NOT-HIT(manual-step): the condition produces a value other than 0 or 1.
float non_step_conditional(float x, float threshold) {
    return x > threshold ? 2.0 : -1.0;
}

// SHOULD-NOT-HIT(sin-cos-pair): sin and cos of DIFFERENT arguments — no sincos opportunity.
float2 sin_cos_different(float a, float b) {
    return float2(sin(a), cos(b));
}

// SHOULD-NOT-HIT(manual-distance): inner subtraction result is further modified
// before the length call, so the pattern is not length(a-b) directly.
float not_distance(float3 a, float3 b, float3 c) {
    return length((a - b) + c);
}

// Entry point — exercise enough paths that the file isn't trivially dead.
float4 ps_negative_lookalikes(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 tex = AlbedoTex.Sample(BilinearSS, uv);
    float3 col = tex.rgb * BaseColor;
    col = lerp_half(col, EmissiveColor);
    col = lerp_runtime_weight(col, EmissiveColor * EmissiveScale, Roughness);
    col += near_identity_scale(col) * Metallic;
    float a = saturate_clamp_pair(Alpha, 0.1, 0.9);
    float p = pow_non_integer(Roughness);
    float3 n = normalize_scaled(float3(uv, 1.0));
    return float4(col * p + n * a, 1.0);
}
