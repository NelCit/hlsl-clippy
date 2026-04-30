// Phase 2 — redundant operations.

float3 nested_saturate(float3 c) {
    // HIT(redundant-saturate): saturate is idempotent.
    return saturate(saturate(c));
}

float3 nested_saturate_split(float3 c) {
    // HIT(redundant-saturate): saturate of an already-saturated value.
    float3 a = saturate(c);
    return saturate(a);
}

float clamp_zero_one(float x) {
    // HIT(clamp01-to-saturate): clamp(x, 0, 1) is saturate.
    return clamp(x, 0.0, 1.0);
}

float clamp_zero_one_explicit(float x) {
    // HIT(clamp01-to-saturate): clamp(x, 0.0, 1.0) is saturate.
    return clamp(x, 0.0, 1.0);
}

float3 nested_normalize(float3 n) {
    // HIT(redundant-normalize): normalize is idempotent on its result.
    return normalize(normalize(n));
}

float3x3 nested_transpose(float3x3 m) {
    // HIT(redundant-transpose): transpose of transpose is the original.
    return transpose(transpose(m));
}

float abs_of_square(float x) {
    // HIT(redundant-abs): x*x is non-negative.
    return abs(x * x);
}

float abs_of_dot_self(float3 v) {
    // HIT(redundant-abs): dot(v,v) is non-negative.
    return abs(dot(v, v));
}

float abs_of_saturate(float x) {
    // HIT(redundant-abs): saturate output is non-negative.
    return abs(saturate(x));
}

float4 entry_main(float4 pos : SV_Position) : SV_Target {
    float3 c = nested_saturate(pos.xyz);
    float3 n = nested_normalize(float3(0, 1, 0));
    float a = abs_of_square(pos.w);
    return float4(c * n * a, 1.0);
}
