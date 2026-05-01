---
id: manual-mad-decomposition
category: math
severity: warn
applicability: suggestion
since-version: v0.2.0
phase: 2
---

# manual-mad-decomposition

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

A multiply followed by an add that the author has split across two statements with a named temporary, in the form `T t = a * b; ... U u = t + c;` where `t` has no other use between its definition and the add. The rule also matches the symmetric `a * b` plus `c` pattern when `a * b` is computed in one statement and added to `c` in a non-adjacent statement, or when the result of the multiply is stored into a struct field or `out` parameter that is then read once into the add. The detection is structural: a multiply expression whose result feeds exactly one add expression downstream, with the two expressions separated by enough statements that the optimiser may not see them as a single basic-block fold candidate. It does not fire when the temporary is genuinely reused (referenced more than once) or when the intermediate is exported across a function boundary.

## Why it matters on a GPU

Fused multiply-add (FMA) is the fundamental arithmetic primitive of every modern GPU. AMD RDNA 3 issues `v_fma_f32` and `v_fmac_f32` as full-rate VALU instructions — one per SIMD32 lane per clock. NVIDIA Turing, Ampere, and Ada Lovelace issue `FFMA` at full FP32 throughput; on Ada the FFMA path is the largest single contributor to the advertised FP32 TFLOPS figure. Intel Xe-HPG `mad` on the EU vector pipe is similarly first-class. When the compiler can see a multiply-then-add chain, it folds the pair into one instruction: half the issue slots, half the register lifetime for the intermediate, and one rounding step instead of two (FMA is more accurate than separate mul + add by IEEE 754-2008 definition).

The fold is reliable inside a single expression — `a * b + c` — but breaks down in subtle ways once the multiply is split into its own statement. HLSL's reference rasteriser semantics permit the fold across statements within a basic block, but in practice the DXC and Slang front-ends both occasionally fail to fold across a wide enough statement gap, across an intervening `if` or loop, or when the intermediate is sunk into a struct field. When the fold fails, the shader pays for two VALU instructions where one would do; over a typical fragment shader with a dozen MAD-shaped expressions, that is enough to push the kernel from 100% issue-rate utilisation into VALU stall territory. On RDNA the separate add also burns an extra cycle of VGPR liveness, which compounds with the architecture's occupancy pressure.

The pattern most often arises during refactoring: a long expression is split into named temporaries for readability, but the refactor crosses the granularity at which the back-end peephole pass operates. The rule surfaces these split sites and lets the author either fuse the expression back (the fix) or annotate the temporary as deliberately separate (because, for example, a debug print or assertion sits between the multiply and the add and should not be optimised away). Both outcomes are improvements over silent under-utilisation of the FMA pipe.

## Examples

### Bad

```hlsl
// Multiply pulled into a named temporary then added later. The fold across the
// gap is not guaranteed; recent DXC/Slang sometimes leaves this as two ops.
float blend_weight(float t, float scale, float bias) {
    float scaled = t * scale;
    // ... unrelated work or just a long blank line ...
    return scaled + bias;
}
```

### Good

```hlsl
// Single MAD-shaped expression — folds into v_fma_f32 / FFMA reliably.
float blend_weight(float t, float scale, float bias) {
    return t * scale + bias;
}
```

## Options

none

## Fix availability

**suggestion** — A rewrite that re-fuses the multiply and add into a single expression is offered, but not auto-applied. The split form may be intentional: a debugger watch on the intermediate, a deliberate rounding boundary (FMA's single-rounding behaviour can change low-bit results in a way the author may have validated against), or an intermediate consumed by a later edit that the tool cannot foresee. Authors should confirm the intermediate is not load-bearing before accepting the fix.

## See also

- Related rule: [pow-to-mul](pow-to-mul.md) — replaces transcendental `pow` with full-rate multiplies that themselves participate in MAD folding
- Related rule: [dot4add-opportunity](dot4add-opportunity.md) — the integer DP4a equivalent of MAD folding for 8-bit dot products
- HLSL intrinsic reference: `mad`, `fma` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-mad-decomposition.md)

*© 2026 NelCit, CC-BY-4.0.*
