---
id: precise-missing-on-iterative-refine
category: math
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# precise-missing-on-iterative-refine

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A loop whose body implements a Newton-Raphson, Halley, or related iterative-refinement step on a floating-point quantity, where the residual update (the term that drives convergence) lacks the `precise` qualifier. Pattern shapes: `for (uint i = 0; i < N; ++i) { x = x - f(x) / fprime(x); }` or the equivalent `x = (x + a / x) * 0.5;` for square root, or `x = x * (1.5 - 0.5 * a * x * x);` for inverse-square-root refinement, where `x` is a plain `float` rather than `precise float`.

## Why it matters on a GPU

GPU compilers for HLSL run aggressive fast-math reordering by default. The optimiser sees `x = x - f(x)/fprime(x)` and is free to: (a) apply mul-add fusion that changes rounding, (b) reorder the subtraction with respect to constant folds, (c) re-associate `x*x*x` to `x*(x*x)` or `pow(x, 3)`, and crucially (d) recognise that across an unbounded number of iterations the algebraic limit collapses certain residuals to zero — at which point the optimiser may fold the entire iteration to the initial guess, on the grounds that `x = x - 0` is identity. On AMD RDNA 2/3, NVIDIA Ada Lovelace, and Intel Xe-HPG the resulting codegen is silently a no-op iteration: the initial guess passes through unchanged. The author intended N rounds of quadratic-convergence refinement; the shader runs zero rounds.

The hazard is most acute in analytic-derivative SDF / collision pipelines where a single Newton step turns an algebraic-distance estimate into a Euclidean-distance answer — the SDF appears to "almost work" because the initial guess is close to correct, but never converges. Ray-marching kernels that depend on a refined hit position see surface aliasing or self-intersection artefacts. Inverse-kinematics solvers see the chain stay in the initial pose. The bug is hard to spot because the shader compiles, runs, and produces visually plausible results that are silently wrong.

The `precise` qualifier on the relevant variable (`precise float x;`) tells the compiler: do not reorder, do not fuse, do not algebraically simplify operations that touch this variable. The cost is one VALU cycle per protected operation that the optimiser would otherwise have folded — typically 2-5 cycles per iteration. The benefit is correct convergence behaviour. The rule fires on the canonical iteration shapes but does not yet detect every iterative kernel; the long-form template-style loop bodies require richer pattern matching that may follow in a later phase.

## Examples

### Bad

```hlsl
// Newton-Raphson rsqrt refinement. Without `precise`, fast-math may fold
// the iteration to the initial guess on Ada / RDNA 3 / Xe-HPG.
float refined_rsqrt(float a) {
    float x = rsqrt(a);
    for (uint i = 0; i < 2; ++i) {
        x = x * (1.5 - 0.5 * a * x * x);   // unprotected — may collapse
    }
    return x;
}
```

### Good

```hlsl
// `precise` blocks the algebraic reordering that would collapse the iteration.
float refined_rsqrt_precise(float a) {
    precise float x = rsqrt(a);
    for (uint i = 0; i < 2; ++i) {
        x = x * (1.5 - 0.5 * a * x * x);   // protected — converges as intended
    }
    return x;
}

// Square-root Newton step:
float refined_sqrt(float a) {
    precise float x = sqrt(a);             // initial guess
    x = (x + a / x) * 0.5;                 // one Newton refinement
    return x;
}
```

## Options

- `iteration-threshold` (integer, default: 2) — the minimum number of refinement iterations the rule requires to fire. Set higher to silence single-step refinements where the convergence collapse is less load-bearing.

## Fix availability

**suggestion** — Adding `precise` is a one-token change but may interact with surrounding fast-math expectations elsewhere in the function. The diagnostic identifies the iteration variable and the residual term so the author can apply the qualifier where it matters.

## See also

- Related rule: [missing-precise-on-pcf](missing-precise-on-pcf.md) — `precise` for percentage-closer-filtering taps
- Related rule: [div-without-epsilon](div-without-epsilon.md) — division-stability hazard in iterative kernels
- Related rule: [redundant-precision-cast](redundant-precision-cast.md) — companion precision rule
- HLSL reference: `precise` qualifier in the DirectX HLSL specification
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/precise-missing-on-iterative-refine.md)

*© 2026 NelCit, CC-BY-4.0.*
