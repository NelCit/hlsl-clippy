---
id: redundant-computation-in-branch
category: control-flow
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 4
---

# redundant-computation-in-branch

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

An expression that appears identically in both the `then`-branch and the `else`-branch of the same `if`/`else` statement, where the expression is a pure computation (no texture samples with implicit derivatives, no writes to UAVs, no calls to intrinsics with side effects) and where all its operands are defined before the `if`. The rule fires when the syntactic form of the expression in both arms is identical and when the data-flow graph confirms that the operands resolve to the same values at the branch point — i.e., no operand is re-assigned between the start of the `if` and the use in either arm. It does not fire when the operands are different (even if the operator and literal constants are the same), when the expression involves a texture sample with implicit gradients (hoisting across a branch boundary would change the derivative context), or when the expression has observable side effects.

## Why it matters on a GPU

When a GPU compiler encounters a predicated `if`/`else` (the default for many short-arm branches on GPU hardware), both arms of the `if` execute unconditionally for every lane, with the results of the non-taken arm discarded by the write mask. In this execution model, a pure expression that appears in both arms executes twice — once in the `then` arm and once in the `else` arm — for every lane, regardless of which arm the lane actually takes. Hoisting the expression to before the `if` eliminates one of the two evaluations, reducing ALU cost by roughly half for that expression.

Even in the case where the compiler emits a real hardware branch (via `[branch]` or its own heuristics), the duplicated expression still imposes code-size cost and prevents the compiler from performing cross-arm common subexpression elimination in its own IR. Some GPU compilers (DXC's backend via LLVM, or the AMD shader compiler's front-end) perform this elimination themselves, but it is not guaranteed across all driver versions and all shader compiler implementations. Writing the hoisted form explicitly makes the intent clear and ensures the elimination is not compiler-dependent.

For expressions that are moderately expensive — a `pow`, a `dot` product on a `float3`, a `normalize`, or a multi-component lerp — the redundancy can represent a significant fraction of the shader's ALU budget. In a PBR material shader where the branch selects between two shading paths but both paths require the same luma weighting (`dot(rgb, float3(0.2126, 0.7152, 0.0722))`) or the same Fresnel term, the duplicated computation shows up directly in the per-pixel ALU count. Hoisting is a zero-risk transformation for pure expressions: the result is identical, and the definition of the hoisted temporary is visible to both arms.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/loop_invariant.hlsl, line 33-44
// HIT(redundant-computation-in-branch): pow(base.a, 5.0) computed
// identically in both arms — hoist it out of the if.
float4 ps_redundant_in_branch(float2 uv : TEXCOORD0) : SV_Target {
    float4 base = Tex.Sample(Bilinear, uv);
    float4 result;
    if (uv.x > 0.5) {
        result = base * 2.0 + pow(base.a, 5.0);   // pow computed here
    } else {
        result = base * 0.5 + pow(base.a, 5.0);   // and here — identical
    }
    return result;
}

// From tests/fixtures/phase4/loop_invariant_extra.hlsl, line 80-94
// HIT(redundant-computation-in-branch): dot(current.rgb, float3(0.299,...))
// identical in both branches — hoist luma computation.
float4 ps_taa_resolve(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 current = BlurTex.SampleLevel(Linear, uv, 0);
    float4 history = NoiseTex.SampleLevel(Linear, uv, 0);
    float4 blended;
    if (current.a > 0.5) {
        float luma = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
        blended = lerp(history, current, 0.1 + luma * 0.05);
    } else {
        float luma = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
        blended = lerp(history, current, 0.9 - luma * 0.05);
    }
    return blended * Exposure;
}
```

### Good

```hlsl
// Hoist the common expression to before the if.
float4 ps_redundant_hoisted(float2 uv : TEXCOORD0) : SV_Target {
    float4 base    = Tex.Sample(Bilinear, uv);
    float  pow_a5  = pow(base.a, 5.0);   // computed once
    float4 result;
    if (uv.x > 0.5) {
        result = base * 2.0 + pow_a5;
    } else {
        result = base * 0.5 + pow_a5;
    }
    return result;
}

// Hoisted luma for TAA resolve.
float4 ps_taa_resolve_fixed(float2 uv : TEXCOORD0, float4 pos : SV_Position) : SV_Target {
    float4 current = BlurTex.SampleLevel(Linear, uv, 0);
    float4 history = NoiseTex.SampleLevel(Linear, uv, 0);
    float  luma    = dot(current.rgb, float3(0.2126, 0.7152, 0.0722));
    float4 blended;
    if (current.a > 0.5) {
        blended = lerp(history, current, 0.1 + luma * 0.05);
    } else {
        blended = lerp(history, current, 0.9 - luma * 0.05);
    }
    return blended * Exposure;
}
```

## Options

none

## Fix availability

**machine-applicable** — When the rule confirms that both arm expressions are syntactically and semantically identical, and that the expression is pure (no side effects, no implicit-gradient intrinsics), it extracts the expression to a new `float` (or vector) temporary immediately before the `if` and replaces both in-arm occurrences with the new name. The substitution is semantically equivalent for all pure expressions. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [cbuffer-load-in-loop](cbuffer-load-in-loop.md) — loop-invariant cbuffer expression reloaded each iteration
- Related rule: [loop-invariant-sample](loop-invariant-sample.md) — loop-invariant texture sample
- Related rule: [branch-on-uniform-missing-attribute](branch-on-uniform-missing-attribute.md) — uniform branch missing [branch] attribute
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-computation-in-branch.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
