// Phase 2 — misc correctness rules (extra patterns).  Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

// --- comparison-with-nan-literal (additional patterns) ---

bool is_inf_check(float x) {
    // HIT(comparison-with-nan-literal): 1.0/0.0 produces +inf; comparing against
    // it with < is still undefined/always-false for NaN inputs.
    return x < (1.0 / 0.0);
}

bool nan_equality(float x) {
    // HIT(comparison-with-nan-literal): NaN != NaN is always true — meaningless.
    return x == (0.0 / 0.0);
}

// SHOULD-NOT-HIT(comparison-with-nan-literal): isnan() is the correct idiom.
bool safe_nan_check(float x) {
    return isnan(x);
}

// --- compare-equal-float (additional patterns) ---

bool depth_exact_equal(float depth, float ref) {
    // HIT(compare-equal-float): exact == on float is NaN / precision fragile.
    return depth == ref;
}

bool alpha_not_one(float a) {
    // HIT(compare-equal-float): != on float; use abs(a - 1.0) > eps instead.
    return a != 1.0;
}

// SHOULD-NOT-HIT(compare-equal-float): integer comparison through uint reinterpret.
bool int_equal(uint a, uint b) {
    return a == b;
}

// SHOULD-NOT-HIT(compare-equal-float): half comparison inside a known-integer context.
bool min16uint_equal(min16uint a, min16uint b) {
    return a == b;
}

// --- redundant-precision-cast (additional patterns) ---

float half_round_trip(float x) {
    // HIT(redundant-precision-cast): float → half → float re-introduces rounding
    // every evaluation; the conversion pair is dead if the value is only read.
    return (float)((half)x);
}

float uint_float_uint(uint x) {
    // HIT(redundant-precision-cast): uint → float → uint drops bits for large values.
    return (float)((uint)((float)x));
}

// SHOULD-NOT-HIT(redundant-precision-cast): intentional precision reduction before
// packing — the half is the *result*, not discarded.
half intentional_half(float x) {
    return (half)x;
}

// SHOULD-NOT-HIT(redundant-precision-cast): the outer cast changes the final type.
int float_to_int(float x) {
    return (int)x;
}

// --- entry point ---

float4 ps_misc_extra(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float  depth = pos.z / pos.w;
    bool   eq    = depth_exact_equal(depth, 0.5);
    bool   nan   = nan_equality(uv.x);
    float  rt    = half_round_trip(uv.y);
    return float4(rt, (float)eq, (float)nan, 1.0);
}
