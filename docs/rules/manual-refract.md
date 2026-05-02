---
id: manual-refract
category: math
severity: warn
applicability: suggestion
since-version: "v0.2.0"
phase: 2
---

# manual-refract

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A return statement whose expression structurally matches the closed-form HLSL implementation of `refract(I, N, eta)`:

```
eta * I - (eta * dot(N, I) + sqrt(...)) * N
```

The heuristic is intentionally conservative. The matched return expression must be a top-level subtraction whose right-hand side is a multiplication of a parenthesised sum (containing both a `sqrt(...)` call and a `dot(N, I)` call where both `dot` arguments are simple vector identifiers) by a vector identifier. The discriminant `k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I))` may either be hoisted to a local and passed to `sqrt(k)`, or inlined directly into the `sqrt(...)` argument — both shapes match because the rule only inspects the structural shape of the return expression itself.

It does not fire on a function that already calls `refract()` directly, on functions that contain only some of the required markers (e.g. a `dot` and `sqrt` but no top-level subtraction), or on lookalike formulas that are missing the parenthesised `(eta * dot(N, I) + sqrt(...))` sum.

## Why it matters on a GPU

The hand-rolled `refract` body decomposes into roughly ten dependent VALU operations on every consumer-class GPU. On AMD **RDNA 2 / RDNA 3** the `dot` lowers to a `v_dot3_f32` (or a pair of `v_fma_f32` ops on architectures without packed dot), each scalar multiply and the trailing vector subtract sit on the regular VALU pipe at one issue per cycle, and the `sqrt` lowers to a transcendental issued through the SFU at one-quarter VALU rate. The chain is dependency-bound: the `sqrt` cannot start until the `dot * dot * eta * eta` chain that feeds `k` retires, and the final vector multiply against `N` cannot start until `sqrt` retires. On NVIDIA **Turing**, **Ampere**, and **Ada Lovelace** the same story plays out on the SM's special-function unit, which runs at 1/4 the FP32 rate. The hand-rolled form therefore stalls on the SFU for `sqrt` even though the surrounding VALU is idle — a textbook latency-bound pattern.

The HLSL `refract()` intrinsic communicates the entire operation as a single high-level call to the compiler. DXC and Slang both lower it to a target-specific instruction sequence: on RDNA the `sqrt` is co-issued with adjacent VALU ops via the SFU port, and on NVIDIA hardware the SFU `sqrt` is overlapped with the `dot` chain by the scheduler. **Intel Xe-HPG** has a similar SFU rate constraint for `sqrt` and benefits from the same scheduling. More importantly, `refract()` is one of the patterns IHV driver compilers actively peephole-recognise; rewriting the body manually defeats that recognition and locks the function into the naïve schedule.

The total-internal-reflection branch (`if (k < 0.0) return float3(0,0,0);`) is the second cost. On **RDNA** that branch becomes a uniform-or-divergent `s_cbranch` depending on whether neighbouring lanes agree on the sign of `k`; in PBR or refraction shaders along grazing surfaces, lanes within a wave often disagree, and the branch becomes execution-mask-divergent — both branches execute and the inactive lanes are masked. The built-in `refract()` lowers to a branchless select on RDNA and on **Ampere/Ada** (`v_cndmask_b32` / equivalent), evaluating both the `eta * I - ... * N` term and the zero result and selecting between them. That is the same total work but no execution-mask spill, so wave occupancy is unaffected. The hand-rolled `if (k < 0)` form pays the divergence cost; the intrinsic does not.

Beyond performance, the manual form is easy to subtly miswrite. The sign of `dot(N, I)` (incident vs. surface-outward normal convention), the placement of `eta` inside vs. outside the parenthesised sum, and the choice of `1.0 - eta * eta * (...)` vs. `eta * eta * (1.0 - ...)` are all common typo surfaces. `refract(I, N, eta)` is verified by the compiler's semantic analysis; a manual reimplementation is not.

## Examples

### Bad

```hlsl
float3 hand_rolled_refract(float3 I, float3 N, float eta) {
    float k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I));
    if (k < 0.0)
        return float3(0, 0, 0);
    return eta * I - (eta * dot(N, I) + sqrt(k)) * N;   // ~10 dependent VALU ops + SFU sqrt
}
```

### Good

```hlsl
float3 transmitted(float3 I, float3 N, float eta) {
    return refract(I, N, eta);
}
```

## Options

none

## Fix availability

**suggestion-only** — Identifying which of the function's parameters plays the role of `I`, `N`, and `eta` from the AST alone is fragile, and codebases differ on the incident-vs-outward normal convention. The diagnostic carries a `Fix` whose description suggests `return refract(I, N, eta);`, but no automatic edits are produced; `hlsl-clippy --fix` will not rewrite this rule unattended.

## See also

- [`manual-reflect`](manual-reflect.md) — sibling rule for the closed-form `reflect(v, n)` formula.
- [`manual-distance`](manual-distance.md) — open-coded `length(a - b)` → `distance(a, b)`.
- HLSL intrinsic reference: `refract` in the DirectX HLSL Intrinsics documentation.
- Companion blog post: [math overview](../blog/math-overview.md).

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-refract.md)

*© 2026 NelCit, CC-BY-4.0.*
