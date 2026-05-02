// ADR 0021 sub-phase B.4 (v1.4.0) — AST coverage smoke fixture.
//
// Hand-curated `.slang` body that uses HLSL-syntax-level constructs that
// tree-sitter-slang preserves verbatim from tree-sitter-hlsl. The fixture
// exercises ~5 representative AST-stage rules so the
// `slang-ast-coverage` CI gate has something concrete to assert against.
// Each pattern below carries `// HIT(rule-id)` for the rule that should
// fire under tree-sitter-slang dispatch.
//
// What this fixture is NOT: it is not a comprehensive test of every
// AST rule on Slang. The fine-grained per-rule audit (B.3 fine, queued
// for v1.4.x → v1.5.0) catalogues the empirical pass-through rate by
// running the full 124 AST + 33 CFG rule registry against a corpus
// drawn from open-source `.slang` shaders.

float test_pow_const_squared(float x) {
    // HIT(pow-const-squared) — pow(x, 2.0) is equivalent to x*x and
    // emits an explicit pow intrinsic. tree-sitter-slang's grammar
    // exposes this as `call_expression` with the HLSL-identical
    // `function` field, so the rule's pattern matches.
    return pow(x, 2.0);
}

float test_pow_to_mul(float x) {
    // HIT(pow-to-mul) — small-integer power decomposition opportunity.
    // Same node-kind chain as above (call_expression / identifier /
    // number_literal); tree-sitter-slang handles it identically.
    return pow(x, 3.0);
}

float test_inv_sqrt_to_rsqrt(float x) {
    // HIT(inv-sqrt-to-rsqrt) — `1.0 / sqrt(x)` should use rsqrt(x).
    // The rule's pattern is a `binary_expression` (operator `/`) over
    // `number_literal` and `call_expression(sqrt)`. Pass-through.
    return 1.0 / sqrt(x);
}

float test_compare_equal_float(float a, float b) {
    // HIT(compare-equal-float) — float == float is unsafe due to FP
    // rounding; rule walks `binary_expression` with operator `==`
    // over float-typed sides. tree-sitter-slang preserves the
    // operator-token surface verbatim.
    if (a == b) {
        return 1.0;
    }
    return 0.0;
}

float test_manual_distance(float3 a, float3 b) {
    // HIT(manual-distance) — `length(a - b)` reimplements `distance`.
    // Two nested `call_expression`s with `binary_expression` between.
    // Slang preserves all three node-kinds.
    return length(a - b);
}

// Slang-only attribute on the entry point. Sub-phase B's parser tolerates
// this construct; the AST below it remains walkable regardless of
// whether the rule's pattern recognises the attribute sibling.
[shader("pixel")]
float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    const float a = test_pow_const_squared(uv.x);
    const float b = test_pow_to_mul(uv.x);
    const float c = test_inv_sqrt_to_rsqrt(uv.x + 1.0);
    const float d = test_compare_equal_float(uv.x, uv.y);
    const float e = test_manual_distance(float3(uv, 0.0), float3(0.0, 0.0, 0.0));
    return float4(a, b + c, d, e);
}
