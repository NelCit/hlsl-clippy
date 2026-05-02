---
id: dead-store-sv-target
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# dead-store-sv-target

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

An assignment to a pixel-shader output variable carrying the `SV_Target` semantic (or to a local variable that is returned as `SV_Target`) where that assignment is immediately overwritten on all paths before the value is returned or used. The first write is a dead store: it is never read, so the computation and the write are wasted. The rule uses Slang's reflection API to identify `SV_Target`-typed outputs and a light data-flow pass to detect write-before-read patterns within the same function scope. See `tests/fixtures/phase3/bindings_extra.hlsl`, lines 42–46 for the `ps_dead_store` example: `float4 result = float4(1,0,0,1)` is written and then immediately overwritten by `result = float4(0,1,0,1)` before `result` is used.

## Why it matters on a GPU

A dead store to an `SV_Target` output variable computes and writes a value that the GPU will never read. The computation of the dead value — any arithmetic, texture samples, or other operations that produced it — is wasted ALU and memory bandwidth. On AMD RDNA and NVIDIA Turing, the compiler may eliminate simple constant dead stores (e.g., `result = float4(1,0,0,1)` followed by an unconditional override), but it cannot in general eliminate dead stores whose right-hand side involves function calls, texture samples, or control-flow-dependent values without a full inter-procedural dead-code elimination pass — which GPU shader compilers rarely perform at the full-program scale.

Beyond the wasted computation, dead stores are a readability and correctness hazard. A developer reading the shader sees two writes to `result` and must determine whether the first write is intentional (e.g., a default value used on some branch) or accidental (a copy-paste artefact). When the first write is always overwritten regardless of control flow, it is definitively dead and its presence misleads anyone auditing the shader for correctness. In complex pixel shaders with many output components, a dead store can also suppress compiler dead-code-elimination of the entire expression that feeds the dead store, keeping alive computations that would otherwise be removed.

The fix is always to delete the dead store and any computation that feeds only the dead store. This is a pure deletion: the return value of the function is unchanged (the surviving write determines the output), and no branch or condition is affected.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings_extra.hlsl, lines 42-46
// HIT(dead-store-sv-target): first write to result is always overwritten.
float4 ps_dead_store(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 result = float4(1, 0, 0, 1);   // dead write — immediately overwritten
    result = float4(0, 1, 0, 1);          // this is the value that actually returns
    return result;
}
```

### Good

```hlsl
// After machine-applicable fix — dead write removed.
float4 ps_dead_store(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 result = float4(0, 1, 0, 1);
    return result;
}

// Or equivalently:
float4 ps_dead_store(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return float4(0, 1, 0, 1);
}
```

## Options

none

## Fix availability

**suggestion** — Conceptually a dead-store fix is a one-line deletion, but the rewrite is *not* safe to auto-apply for two reasons:

1. **Multi-statement, multi-line edit with leading whitespace.** Mechanically removing only the assignment-byte-range leaves a dangling line of indentation and (when the dead store is the variable's *declaration with initializer*) elides the type, breaking the surviving write. The detector flags the assignment span; cleanly excising the statement requires walking back to the previous statement boundary and forward through trailing whitespace, which the current pattern-match detector does not do.
2. **Right-hand side may have side effects.** The detection is structural — it counts adjacent writes at the same brace depth — and does not inspect the RHS expression. A dead store of the form `o.color = expensive_call();` is dead with respect to the output, but `expensive_call()` may have observable side effects (UAV writes, atomics, `printf`-style debug intrinsics) that the developer relied on. Auto-removing the statement would silently drop those calls.

The lint surfaces the dead write; the deletion needs human review to confirm the RHS is side-effect-free and to clean up the surrounding whitespace. If you want the rule to emit a TextEdit you can review-and-apply by hand, that is tracked as a follow-up.

## See also

- Related rule: [unused-cbuffer-field](unused-cbuffer-field.md) — cbuffer field declared but never read
- Related rule: [rwresource-read-only-usage](rwresource-read-only-usage.md) — RW resource only ever read
- HLSL `SV_Target` semantic documentation in the DirectX HLSL reference
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/dead-store-sv-target.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
