// Phase 2 — redundant operations (extra patterns). Hand-written fixture for hlsl-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.
// Note: redundant-saturate and clamp01-to-saturate are covered in redundant.hlsl.

// --- redundant-normalize ---

float3 renormalize_after_normalize(float3 n) {
    // HIT(redundant-normalize): normalize(normalize(x)) — outer call is a no-op.
    return normalize(normalize(n));
}

float3 renormalize_split(float3 v) {
    // HIT(redundant-normalize): normalizing an already-unit result from normalize().
    float3 u = normalize(v);
    return normalize(u);
}

// SHOULD-NOT-HIT(redundant-normalize): normalizing a vector that came from scaling,
// which can change its length even if it was once normalized.
float3 normalize_after_scale(float3 n, float scale) {
    return normalize(n * scale);
}

// --- redundant-transpose ---

float4x4 double_transpose(float4x4 M) {
    // HIT(redundant-transpose): transpose(transpose(M)) == M.
    return transpose(transpose(M));
}

float3x3 double_transpose_3x3(float3x3 M) {
    // HIT(redundant-transpose): redundant on 3x3 as well.
    return transpose(transpose(M));
}

// SHOULD-NOT-HIT(redundant-transpose): single transpose is meaningful.
float4x4 single_transpose(float4x4 M) {
    return transpose(M);
}

// --- redundant-abs ---

float abs_of_even_power(float x) {
    // HIT(redundant-abs): x*x*x*x is non-negative (even exponent).
    float sq = x * x;
    return abs(sq * sq);
}

float abs_of_length_sq(float3 v) {
    // HIT(redundant-abs): dot(v, v) is always >= 0.
    return abs(dot(v, v));
}

float abs_of_exp(float x) {
    // HIT(redundant-abs): exp(x) is always positive; abs is dead.
    return abs(exp(x));
}

// SHOULD-NOT-HIT(redundant-abs): dot of two different vectors can be negative.
float abs_of_dot(float3 a, float3 b) {
    return abs(dot(a, b));
}

// SHOULD-NOT-HIT(redundant-abs): signed linear expression.
float abs_of_linear(float x) {
    return abs(x - 0.5);
}

// --- entry point ---

float4 ps_redundant_extra(float4 pos : SV_Position) : SV_Target {
    float3 n = renormalize_after_normalize(pos.xyz);
    float4x4 M = (float4x4)1;
    float4x4 Md = double_transpose(M);
    float  a = abs_of_even_power(pos.w);
    float  b = abs_of_length_sq(n);
    return float4(n * a + b, 1.0) + Md[0];
}
