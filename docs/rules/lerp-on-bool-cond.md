---
id: lerp-on-bool-cond
category: math
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# lerp-on-bool-cond

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

Calls to `lerp(a, b, t)` where `t` is a value of `bool` (or vector-of-bool) type that has been cast to a floating-point type at the call site — most commonly `lerp(a, b, (float)cond)` or `lerp(a, b, cond ? 1.0 : 0.0)`. The rule matches both the explicit C-style cast `(float)cond` and the construction-style `float(cond)`, and recognises the ternary-of-zero-and-one pattern as a syntactic fingerprint for the same intent. It does not fire on `lerp` calls whose third argument is a genuine continuous parameter (a UV coordinate, a fraction, a `saturate(...)` result), because that is the operation `lerp` exists to express.

## Why it matters on a GPU

`lerp(a, b, t)` lowers in DXIL to an `FMad` of the form `a + (b - a) * t` — typically two FP32 operations: one subtract for `b - a` and one fused multiply-add. On AMD RDNA 2/3 that is one `v_sub_f32` plus one `v_fma_f32` issued at full VALU rate; on NVIDIA Turing/Ada it is one `FADD`/`FFMA` pair; on Intel Xe-HPG, the same shape on the EU's FMA pipeline. When `t` is a true floating-point fraction, this is exactly the right code. When `t` is a Boolean coerced to 0.0 or 1.0, the same two instructions execute but they degenerate into a select: `t == 0` returns `a`, `t == 1` returns `b`, and the multiply-add throws away one of the operands by multiplying it by zero.

The portability problem is that different driver back-ends spot this degeneration with different reliability. Modern DXC on DXIL frequently rewrites `lerp(a, b, (float)cond)` into a `select` (DXIL has a first-class `select` opcode and AMD/NVIDIA back-ends both lower it to one VALU); but the SPIR-V back-end (for Vulkan), the Metal Shading Language back-end, and older driver compilers may not, especially when the cast is hidden behind a function boundary or a macro. The result is one architecture running a single `v_cndmask_b32` (RDNA) or `ISETP`/`SEL` (NVIDIA) and another running a sub + fma. The performance gap is small per call site (one VALU vs two), but it is observable in inner loops and, more importantly, the codegen is non-uniform across targets — exactly the surface this linter exists to flag.

A second, sharper concern is numerical: `a + (b - a) * 1.0` is *not* exactly equal to `b` in IEEE-754 when `a` and `b` differ enough that `b - a` rounds. The cast-to-float pattern silently introduces an ULP-level error at the `cond == true` endpoint that the ternary-or-`select` form does not. For colour blending and UI compositing where the `cond == true` branch should produce the exact `b` value (a fully opaque pixel, a fully selected highlight), the lerp form can leak a one-bit difference that breaks pixel-equality screenshot tests. The portable, fast, exact form is `cond ? b : a` (or, when explicit codegen control matters, the HLSL `select(cond, b, a)` intrinsic on SM 6.0+).

## Examples

### Bad

```hlsl
// Two VALU + an ULP of error at the true endpoint, depending on back-end.
float3 highlight_or_base(bool selected, float3 base, float3 highlight) {
    return lerp(base, highlight, (float)selected);
}

// The ternary-of-0-and-1 spelling is the same anti-pattern with a different mask.
float opacity(bool visible, float fadedAlpha) {
    return lerp(0.0, fadedAlpha, visible ? 1.0 : 0.0);
}
```

### Good

```hlsl
// One VALU, exact at both endpoints, identical codegen on every back-end.
float3 highlight_or_base(bool selected, float3 base, float3 highlight) {
    return selected ? highlight : base;
}

// Or, equivalently, the explicit select intrinsic on SM 6.0+:
float opacity(bool visible, float fadedAlpha) {
    return select(visible, fadedAlpha, 0.0);
}
```

## Options

none

## Fix availability

**machine-applicable** (since v1.2 — ADR 0019) — The fix rewrites `lerp(a, b, (float)cond)` to `cond ? b : a` (and the inverted-ternary form `lerp(a, b, cond ? 0 : 1)` to `cond ? a : b`).

The rewrite is machine-applicable when **both `a` and `b` classify as side-effect-free** under the v1.2 purity oracle (`core/src/rules/util/purity_oracle.{hpp,cpp}`): bare identifiers, field/subscript loads, allowlisted-pure intrinsic calls (textbook math + bit-twiddle + `as*` reinterpret casts) over pure args. The `?:` rewrite drops one of `{a, b}` along each branch, so safety requires both sides to be free of observable side effects.

The fix downgrades to **suggestion-only** when either operand contains a non-allowlisted call (e.g. `g(x)` for an unknown user function), an assignment, an increment/decrement, or any other side-effectful sub-expression. Hand-review the rewrite in that case: applying it would change the call count or evaluation count of the impure operand, which is observable.

## See also

- Related rule: [lerp-extremes](lerp-extremes.md) — flags `lerp(a, b, 0)` / `lerp(a, b, 1)` constant-fold opportunities
- Related rule: [select-vs-lerp-of-constant](select-vs-lerp-of-constant.md) — companion rule for the both-endpoints-are-constants case
- Related rule: [manual-step](manual-step.md) — surfaces the analogous step/mix anti-pattern
- HLSL intrinsic reference: `lerp`, `select`, ternary operator in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/lerp-on-bool-cond.md)

*© 2026 NelCit, CC-BY-4.0.*
