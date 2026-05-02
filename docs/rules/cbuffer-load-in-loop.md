---
id: cbuffer-load-in-loop
category: control-flow
severity: warn
applicability: machine-applicable
since-version: v0.4.0
phase: 4
---

# cbuffer-load-in-loop

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Reads from a `cbuffer` (or `ConstantBuffer<T>`) field — or any arithmetic expression whose operands are exclusively cbuffer fields and numeric literals — inside a loop body, when the expression is loop-invariant: it does not depend on the loop induction variable or any value defined inside the loop. The rule fires on repeated use of the same cbuffer field or constant-folded cbuffer expression within a loop (e.g., `Sigma * Sigma`, `NearZ`, `FarZ - NearZ`) where the field itself is not indexed by the loop counter. It does not fire when the cbuffer read is through a loop-counter-dependent index (e.g., `LightArray[i].position`), or when the field's value changes inside the loop body (which cannot happen with cbuffer reads, but may happen with local variable aliasing).

## Why it matters on a GPU

Cbuffer data resides in a dedicated constant buffer cache — a small, high-bandwidth, read-only cache separate from L1/L2 texture and UAV caches. On AMD RDNA and RDNA 2/3, the constant cache (also called the scalar cache or K-cache) is accessed via the scalar register file (SGPRs). The hardware is architecturally designed for the case where every lane in a wave reads the same cbuffer value simultaneously, which it does for any truly uniform constant: one scalar load fills an SGPR, and that SGPR value is broadcast to all 32 or 64 lanes without consuming per-lane VGPR space. In practice, the cbuffer value is loaded into an SGPR once per wave (or per draw call in the driver's implementation) and cached there — no repeated cache requests occur.

However, when the cbuffer read appears inside a loop body in source code, the compiler cannot always determine whether it is safe to hoist the load to before the loop. This is because the compiler's alias analysis must account for potential writes to the cbuffer binding from the API level (i.e., the CPU could in principle have updated the cbuffer binding between two shader invocations), and some HLSL compiler implementations conservatively treat each in-loop cbuffer access as a potential cache reload. Even when the load does reach the scalar cache on every access, the repeated explicit load instruction consumes issue slots in the SGPR pipeline that could otherwise be used for actual arithmetic. For computed expressions like `Sigma * Sigma` (a SMUL followed by a scalar store), the expression is evaluated fresh each iteration rather than once.

The more important case is when the compiler does not hoist the load and the expression is a computed value: `Sigma * Sigma` uses two SGPR reads plus a scalar VALU multiply each iteration. For a 128-tap Gaussian blur loop, this means 128 multiplies that produce the identical result every time. Hoisting to a local `float sigma2 = Sigma * Sigma;` before the loop eliminates 127 of the 128 multiplies and makes the compiler's work trivially correct: the local variable is defined outside the loop, and the compiler needs no alias analysis to know it is invariant. For cbuffer fields used directly (no computation), the hoist eliminates the explicit load instruction inside the loop body, giving the scheduler more freedom to issue the arithmetic ops that follow.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/loop_invariant.hlsl, line 23-31
// HIT(cbuffer-load-in-loop): Radius * Radius is loop-invariant; load
// once into a temp before the loop.
float4 ps_cbuffer_load_in_loop(float2 uv : TEXCOORD0) : SV_Target {
    float r2 = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        // Radius is a cbuffer field — evaluated every iteration.
        r2 += (Radius * Radius) * 0.125;
    }
    return float4(r2, r2, r2, 1);
}

// From tests/fixtures/phase4/loop_invariant_extra.hlsl, line 64-76
// HIT(cbuffer-load-in-loop): Sigma reloaded from cbuffer every iteration;
// hoist `Sigma * Sigma` into a local before the loop.
void cs_gaussian_blur(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    // ...
    for (uint i = 0; i < TapCount; ++i) {
        float g = exp(-(float)i * (float)i / (2.0 * Sigma * Sigma));
        // HIT(cbuffer-load-in-loop): NearZ also reloaded every iteration.
        float linearDepth = NearZ / max((float)i + 1.0, NearZ);
        // ...
    }
}
```

### Good

```hlsl
// Hoist the invariant expression to a local before the loop.
float4 ps_cbuffer_hoisted(float2 uv : TEXCOORD0) : SV_Target {
    float radius2 = Radius * Radius;   // computed once
    float r2 = 0;
    [unroll] for (int i = 0; i < 8; ++i) {
        r2 += radius2 * 0.125;         // VGPR/SGPR read — no cbuffer access
    }
    return float4(r2, r2, r2, 1);
}

// Hoist both invariant expressions before the Gaussian blur loop.
void cs_gaussian_blur_hoisted(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    float sigma2 = Sigma * Sigma;   // hoist cbuffer-derived expression
    float nearZ  = NearZ;           // hoist plain cbuffer field
    // ...
    for (uint i = 0; i < TapCount; ++i) {
        float g = exp(-(float)i * (float)i / (2.0 * sigma2));
        float linearDepth = nearZ / max((float)i + 1.0, nearZ);
        // ...
    }
}
```

## Options

none

## Fix availability

**machine-applicable** — When the rule confirms that the expression is a pure function of cbuffer fields and literals (no texture reads, no UAV accesses, no side effects), it inserts a `float <name> = <expr>;` local before the loop and replaces the in-loop occurrences with the new local name. The expression is identical; only the evaluation point changes. Because cbuffer fields are read-only from the shader's perspective and the expression has no side effects, the substitution is always semantically equivalent. `hlsl-clippy fix` applies it without human confirmation.

## See also

- Related rule: [loop-invariant-sample](loop-invariant-sample.md) — texture sample inside loop with loop-invariant UV (TMU variant)
- Related rule: [redundant-computation-in-branch](redundant-computation-in-branch.md) — same expression in both arms of if/else
- Related rule: [small-loop-no-unroll](small-loop-no-unroll.md) — constant-bounded loop without [unroll]
- HLSL language reference: `cbuffer`, `ConstantBuffer<T>` in the DirectX HLSL documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/cbuffer-load-in-loop.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
