---
id: flatten-on-uniform-branch
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# flatten-on-uniform-branch

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

The `[flatten]` attribute applied to an `if` / `else` whose condition is dynamically uniform across the wave (or thread group, depending on stage). Uniformity is established by the rule's existing wave-uniformity analysis: cbuffer scalars, constants, `WaveReadLaneFirst` results, and any expression provably independent of `SV_DispatchThreadID` / `SV_GroupThreadID` / per-lane attribute interpolation. Shares the uniformity machinery with the locked `branch-on-uniform-missing-attribute` rule.

## Why it matters on a GPU

The `[flatten]` and `[branch]` attributes are HLSL hints that tell the compiler how to lower an `if` / `else` to GPU instructions. `[flatten]` says: evaluate both arms unconditionally and select between them with a predicate (no jump, no divergence handling — both arms always run). `[branch]` says: emit a real conditional jump and let the lanes that take the false path skip the true arm's instructions entirely. The two have very different cost models: `[flatten]` is cheaper when the arms are short and the branch would otherwise cost more in divergence handling than the work the skipped arm performs; `[branch]` is cheaper when the arms are non-trivial and the predicate is uniform across the wave (so all lanes take the same path and the inactive arm's instructions are genuinely skipped).

When the predicate is *uniform*, the arithmetic flips: `[branch]` lets the wave skip the inactive arm entirely (full cycle savings on every instruction in the dead arm), while `[flatten]` forces the wave to execute both arms and pick one. On AMD RDNA 2/3 a `[flatten]` on a 10-instruction true-arm + 10-instruction false-arm with a uniform predicate burns 20 instructions per wave when 10 would suffice. NVIDIA Ada applies the same penalty per warp; Intel Xe-HPG behaves similarly. The hint was almost always written as a defensive copy-paste from a divergent context — the developer once needed `[flatten]` for a per-lane predicate elsewhere and applied it to every branch in the file. Uniform branches are exactly where the hint's cost model inverts.

The fix is to switch the attribute to `[branch]` (or remove it and let the compiler choose, which on uniform branches will typically pick the right one). The rule does not auto-fix because the developer may have other reasons for the choice — for example, an attempt to keep a hot path branch-free for predictability across drivers. The diagnostic surfaces the uniformity proof and the attribute so the author can decide. The rule is restricted to *dynamically* uniform predicates; statically constant predicates are folded by the optimiser regardless of attribute and are out of scope.

## Examples

### Bad

```hlsl
cbuffer Globals { uint g_Mode; };

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    // g_Mode comes from a cbuffer — wave-uniform across every dispatch.
    // [flatten] forces both arms to execute every wave.
    [flatten]
    if (g_Mode == MODE_FANCY) {
        return shade_fancy(uv);   // 30+ ALU ops always run
    } else {
        return shade_simple(uv);  // and so does this arm — wasted on every wave
    }
}
```

### Good

```hlsl
cbuffer Globals { uint g_Mode; };

float4 ps_main_branch(float2 uv : TEXCOORD0) : SV_Target {
    // [branch] lets the wave actually skip the inactive arm when g_Mode is uniform.
    [branch]
    if (g_Mode == MODE_FANCY) {
        return shade_fancy(uv);
    } else {
        return shade_simple(uv);
    }
}

// Or drop the attribute entirely; on a uniform predicate the compiler will
// select the right lowering.
float4 ps_main_no_attr(float2 uv : TEXCOORD0) : SV_Target {
    if (g_Mode == MODE_FANCY) {
        return shade_fancy(uv);
    } else {
        return shade_simple(uv);
    }
}
```

## Options

none

## Fix availability

**suggestion** — Switching `[flatten]` to `[branch]` is a one-token change but may surface compiler-version differences in lowering. The diagnostic identifies the attribute and the uniformity proof so the author can apply the change.

## See also

- Related rule: [branch-on-uniform-missing-attribute](branch-on-uniform-missing-attribute.md) — sibling rule for the missing-attribute case
- Related rule: [forcecase-missing-on-ps-switch](forcecase-missing-on-ps-switch.md) — `switch` analogue
- Related rule: [redundant-computation-in-branch](redundant-computation-in-branch.md) — wasted work in branch arms
- HLSL reference: `[flatten]` / `[branch]` attributes in the DirectX HLSL specification
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/flatten-on-uniform-branch.md)

*© 2026 NelCit, CC-BY-4.0.*
