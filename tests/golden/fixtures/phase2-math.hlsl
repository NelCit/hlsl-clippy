// Phase 2 — math simplification rules.
// All HIT lines are the rule firings we expect from the linter.

float3 fresnel_schlick(float n_dot_v, float3 f0) {
    // HIT(pow-to-mul): pow(x, 5.0) is the canonical Schlick form; decompose.
    float k = pow(1.0 - n_dot_v, 5.0);
    return f0 + (1.0 - f0) * k;
}

float exponential_falloff(float t) {
    // HIT(pow-base-two-to-exp2): pow(2.0, x) is exp2(x).
    return pow(2.0, -t * 8.0);
}

float pow_squared(float x) {
    // HIT(pow-to-mul): pow(x, 2.0) is x*x.
    return pow(x, 2.0);
}

float pow_cubed(float x) {
    // HIT(pow-to-mul): pow(x, 3.0) is x*x*x.
    return pow(x, 3.0);
}

float pow_decomposable(float x) {
    // HIT(pow-integer-decomposition): pow(x, 5.0) → x*x*x*x*x.
    return pow(x, 5.0);
}

float inverse_length(float3 v) {
    // HIT(inv-sqrt-to-rsqrt): 1/sqrt(x) is rsqrt(x).
    return 1.0 / sqrt(dot(v, v));
}

float3 lerp_zero_endpoint(float3 a, float3 b) {
    // HIT(lerp-extremes): t == 0 collapses to a.
    return lerp(a, b, 0.0);
}

float3 lerp_one_endpoint(float3 a, float3 b) {
    // HIT(lerp-extremes): t == 1 collapses to b.
    return lerp(a, b, 1.0);
}

float3 mul_identities(float3 v, float k) {
    // HIT(mul-identity): v * 1.0
    float3 a = v * 1.0;
    // HIT(mul-identity): v + 0.0
    float3 b = a + 0.0;
    // HIT(mul-identity): v * 0.0
    float3 c = b * 0.0;
    return a + b + c + k;
}

float2 sin_cos_of_same(float angle) {
    // HIT(sin-cos-pair): both sin and cos of `angle` — use sincos().
    float s = sin(angle);
    float c = cos(angle);
    return float2(s, c);
}

float3 manual_reflect(float3 v, float3 n) {
    // HIT(manual-reflect): v - 2 * dot(n,v) * n is the reflect() formula.
    return v - 2.0 * dot(n, v) * n;
}

float manual_distance(float3 a, float3 b) {
    // HIT(manual-distance): length(a - b) is distance(a, b).
    return length(a - b);
}

float manual_step(float x, float threshold) {
    // HIT(manual-step): conditional 0/1 is step().
    return x > threshold ? 1.0 : 0.0;
}

float manual_smoothstep(float a, float b, float t) {
    // HIT(manual-smoothstep): cubic Hermite interpolation is smoothstep().
    float n = saturate((t - a) / (b - a));
    return n * n * (3.0 - 2.0 * n);
}

bool inside_unit_sphere(float3 p) {
    // HIT(length-comparison): length(p) < 1 → dot(p,p) < 1.
    return length(p) < 1.0;
}

bool float_equality(float x) {
    // HIT(compare-equal-float): == on float is NaN-fragile.
    return x == 0.0;
}

bool nan_literal_compare(float x) {
    // HIT(comparison-with-nan-literal): comparison with NaN is always false.
    return x < (0.0 / 0.0);
}

float redundant_precision_round_trip(float x) {
    // HIT(redundant-precision-cast): float → int → float drops fraction silently.
    return (float)((int)x);
}

float4 entry_main(float4 pos : SV_Position) : SV_Target {
    float3 n = normalize(float3(0, 1, 0));
    float3 v = normalize(float3(0, 0, 1));
    float3 f = fresnel_schlick(saturate(dot(n, v)), float3(0.04, 0.04, 0.04));
    float falloff = exponential_falloff(pos.x * 0.01);
    float k = pow_decomposable(pos.y * 0.01);
    return float4(f * falloff * k, 1.0);
}
