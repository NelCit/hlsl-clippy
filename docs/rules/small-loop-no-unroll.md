---
id: small-loop-no-unroll
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# small-loop-no-unroll

> **Status:** pre-v0 — rule scheduled for Phase 4; see [ROADMAP](../../ROADMAP.md).

## What it detects

`for` or `while` loops whose trip count is a compile-time constant (a literal integer, a `static const` expression, or an expression that reduces to a constant at compile time) and whose trip count is at or below the configured threshold (`max-iterations`, default 8), when the loop does not carry a `[unroll]` or `[unroll(N)]` attribute. The rule fires on loops whose bounds are fully determined at parse time; it does not fire on loops whose trip count depends on a cbuffer field, a function parameter, or any non-constant expression, even if that expression always evaluates to a small value at runtime.

## Why it matters on a GPU

A GPU loop compiled without `[unroll]` generates real branch instructions: a counter decrement, a compare, and a conditional backward branch at the bottom of each iteration. On a wave of 32 or 64 lanes, this overhead is paid once per iteration but amortises across all lanes simultaneously — the branch is uniform, so there is no divergence penalty, and the branch predictor (on hardware that has one) can predict it with high accuracy for small-count loops. However, the overhead is still non-zero: the counter update and compare consume ALU cycles, the backward edge occupies fetch bandwidth, and the loop carries a data dependency on the counter variable that can limit instruction-level parallelism within the loop body.

More importantly, a compiler that does not see `[unroll]` may decline to unroll the loop even when it is obviously safe to do so. This matters for several reasons. First, unrolled code exposes the full instruction stream to the compiler's scheduler, which can then interleave instructions from different iterations to hide memory latency — a texture sample from iteration 0 can issue while the ALU from a previous iteration is completing. A rolled loop cannot benefit from this cross-iteration scheduling because the scheduler sees only one iteration's worth of instructions at a time. Second, unrolled loops allow the compiler to fold constant folding across iterations — loop indices become literals, and expressions like `(float)i * 0.01` reduce to distinct constants that can be embedded as immediates rather than computed via multiply. Third, very small loops (2-4 iterations) often expose to the back-end that some instructions are dead across all iterations, enabling elimination that the loop form prevents.

On AMD RDNA, RDNA 2/3 and NVIDIA Turing/Ada, the cost of a four-iteration texture-sample loop with no `[unroll]` includes four branch instructions (one per iteration end) plus four cycle costs for the counter update — overhead that, for a loop body consisting of a single `Sample` call, can represent 20-30% of the total per-pixel cost when the TMU returns quickly. Adding `[unroll]` is safe for constant-bounded loops: the semantics are identical, and the compiler is free to re-roll if it determines that register pressure outweighs the benefit (though in practice it rarely does for very small trip counts). The `[unroll(N)]` form can optionally specify a partial unroll factor if the default full unroll exceeds register budget.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 66-73
// HIT(small-loop-no-unroll): 4 iterations, constant-bounded, not unrolled.
float4 ps_small_loop_no_unroll(float2 uv : TEXCOORD0) : SV_Target {
    float4 sum = 0;
    for (int i = 0; i < 4; ++i) {
        sum += Tex.Sample(Bilinear, uv + (float)i * 0.01);
    }
    return sum;
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 52-58
// HIT(small-loop-no-unroll): 3 iterations, constant-bounded, not unrolled.
float4 ps_box_blur_3(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 acc = 0;
    for (int i = -1; i <= 1; ++i) {
        acc += ColorTex.Sample(BilinSS, uv + float2((float)i * 0.001, 0));
    }
    return acc / 3.0;
}
```

### Good

```hlsl
// Add [unroll] — compiler eliminates loop overhead and exposes cross-iteration scheduling.
float4 ps_small_loop_unrolled(float2 uv : TEXCOORD0) : SV_Target {
    float4 sum = 0;
    [unroll] for (int i = 0; i < 4; ++i) {
        sum += Tex.Sample(Bilinear, uv + (float)i * 0.01);
    }
    return sum;
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 62-68
// SHOULD-NOT-HIT(small-loop-no-unroll): [unroll] is explicitly requested.
float4 ps_box_blur_3_unrolled(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 acc = 0;
    [unroll] for (int i = -1; i <= 1; ++i) {
        acc += ColorTex.Sample(BilinSS, uv + float2((float)i * 0.001, 0));
    }
    return acc / 3.0;
}
```

## Options

- `max-iterations` (integer, default: 8) — loops with a constant trip count at or below this value trigger the rule. Set to 0 to disable the rule entirely. Set to a higher value (e.g., 16 or 32) if your shader targets hardware with abundant register files where unrolling larger loops is beneficial; reduce to 4 if register pressure is a concern on your target.

## Fix availability

**suggestion** — The fix inserts `[unroll]` immediately before the `for` keyword. It is shown as a suggestion rather than machine-applied because `[unroll]` increases register pressure: if the loop body uses many temporaries and the unrolled form allocates more VGPRs than the hardware allows for the targeted wave occupancy, the shader may fail to compile or may reduce occupancy. The suggestion is safe in the large majority of cases for trip counts up to 8 with typical loop bodies, but manual confirmation is recommended for heavily loaded shaders.

## See also

- Related rule: [branch-on-uniform-missing-attribute](branch-on-uniform-missing-attribute.md) — analogous attribute-hint rule for uniform branches
- Related rule: [loop-invariant-sample](loop-invariant-sample.md) — texture sample inside a loop with loop-invariant UV
- Related rule: [cbuffer-load-in-loop](cbuffer-load-in-loop.md) — loop-invariant cbuffer field reload each iteration
- HLSL attribute reference: `[unroll]`, `[unroll(N)]`, `[loop]` in the DirectX HLSL Attributes documentation
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/small-loop-no-unroll.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
