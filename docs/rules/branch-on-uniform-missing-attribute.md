---
id: branch-on-uniform-missing-attribute
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# branch-on-uniform-missing-attribute

> **Status:** pre-v0 — rule scheduled for Phase 4; see [ROADMAP](../../ROADMAP.md).

## What it detects

`if` statements whose condition is provably dynamically uniform — derived exclusively from `cbuffer` fields, `nointerpolation` interpolants, `SV_GroupID` (group-level uniform), or other per-dispatch constants — when the `if` statement is not annotated with the `[branch]` attribute. The rule fires on any `if`, `else if`, or `switch` where the condition expression contains no per-thread-varying terms and where no `[branch]` or `[flatten]` attribute is present. It does not fire when `[branch]` is already present, when `[flatten]` is intentional (the user explicitly requested predication), or when the condition involves non-uniform values such as `SV_DispatchThreadID`, `SV_GroupIndex`, interpolated vertex attributes, or texture reads with varying coordinates.

## Why it matters on a GPU

Modern GPU compilers face a choice when generating code for an `if` statement: emit a real conditional branch instruction, or emit predicated (masked) execution of both arms followed by a select. Without explicit guidance, the compiler uses its own heuristics — typically favouring predication for short arms and branching for long ones, tuned for a specific target GPU and register file model. These heuristics are not always correct for the caller's use case, and they can vary between driver versions.

When the branch condition is dynamically uniform — the same value for every lane in the wave simultaneously — a real hardware branch is strictly better than predication. With a real branch, all lanes skip the non-taken arm entirely: zero ALU, zero memory traffic, zero texture unit activity in the skipped arm. With predication, both arms execute on every lane; the results from the non-taken arm are discarded by the write mask, but the execution latency and register pressure from both arms are paid in full. On a mode-select shader (tone mapper, resolve pass, quality-tier switch) where the branch condition is a `cbuffer uint Mode` field that is set once per draw call, predication doubles the ALU cost of every shader invocation across the entire screen. On RDNA 3, a predicated form of a three-way tone-mapper branch executes all three paths per wave and then selects; a real branch skips two-thirds of the work entirely.

The `[branch]` attribute in HLSL is a compiler hint — it requests that the driver emit a real branch instruction rather than predication. On uniform conditions, this hint is always safe: because all lanes agree on the condition, there is no intra-wave divergence, no dead-time from reconvergence, and no serialisation penalty. The compiler is free to hoist the condition evaluation out of the wave loop and branch at the draw-call or dispatch level. Adding `[branch]` to a uniform condition is therefore a low-risk, high-reward annotation for any shader that selects among multiple non-trivial code paths based on a cbuffer flag.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 55-64
// HIT(branch-on-uniform-missing-attribute): Mode is uniform; tagging
// [branch] tells the compiler to emit a real branch instead of predication.
float4 ps_branch_missing_attribute(float4 pos : SV_Position) : SV_Target {
    if (Mode == 0) {
        return float4(1, 0, 0, 1);
    } else if (Mode == 1) {
        return float4(0, 1, 0, 1);
    }
    return float4(0, 0, 1, 1);
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 23-36
// HIT(branch-on-uniform-missing-attribute): Mode is uniform across all lanes;
// without [branch] the compiler may predicate both arms on every thread.
float4 ps_tonemap_mode(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 hdr = ColorTex.Sample(BilinSS, uv);
    float3 col;
    if (Mode == 0u) {
        col = hdr.rgb * Exposure;
    } else if (Mode == 1u) {
        col = hdr.rgb / (hdr.rgb + 1.0);
    } else {
        col = pow(hdr.rgb, 1.0 / 2.2);
    }
    return float4(col, hdr.a);
}
```

### Good

```hlsl
// Add [branch] to request real branching on the uniform condition.
float4 ps_branch_with_attribute(float4 pos : SV_Position) : SV_Target {
    [branch] if (Mode == 0) {
        return float4(1, 0, 0, 1);
    } else if (Mode == 1) {
        return float4(0, 1, 0, 1);
    }
    return float4(0, 0, 1, 1);
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 39-48
// SHOULD-NOT-HIT(branch-on-uniform-missing-attribute): [branch] is already present.
float4 ps_tonemap_mode_ok(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 hdr = ColorTex.Sample(BilinSS, uv);
    float3 col;
    [branch] if (Mode == 0u) {
        col = hdr.rgb * Exposure;
    } else {
        col = hdr.rgb / (hdr.rgb + 1.0);
    }
    return float4(col, hdr.a);
}
```

## Options

none

## Fix availability

**suggestion** — The fix inserts a `[branch]` attribute immediately before the `if` keyword. This is a textual suggestion; verification requires confirming that the branch condition is in fact dynamically uniform for all intended dispatch configurations. In a case where the condition is uniform by calling convention (e.g., the `cbuffer` field is always set identically for all threads) but not enforced by type system, the suggestion is shown for human review rather than applied automatically.

## See also

- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — non-uniform control flow causing incorrect wave operation results
- Related rule: [small-loop-no-unroll](small-loop-no-unroll.md) — analogous attribute-hint rule for constant-bounded loops
- HLSL attribute reference: `[branch]`, `[flatten]` in the DirectX HLSL Attributes documentation
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/branch-on-uniform-missing-attribute.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
