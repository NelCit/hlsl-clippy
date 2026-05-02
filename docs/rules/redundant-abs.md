---
id: redundant-abs
category: saturate-redundancy
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
---

# redundant-abs

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls to `abs(expr)` where the enclosed expression is statically provable to be non-negative. The rule currently recognises three sub-patterns:

1. `abs(x * x)` — a value squared is always non-negative for real floats.
2. `abs(dot(v, v))` — the self-dot-product of any vector equals the sum of squared components, which is non-negative.
3. `abs(saturate(expr))` — the output of `saturate` is clamped to [0, 1] and is therefore non-negative by definition.

The rule does not fire on `abs(x * y)` when `x` and `y` are distinct expressions, nor on `abs(dot(u, v))` for distinct vectors, since those products can be negative. The match is structural: only the specific patterns listed above trigger the diagnostic.

## Why it matters on a GPU

`abs` in HLSL is not a free operation when applied to an arbitrary VGPR value. On AMD RDNA, RDNA 2, and RDNA 3, `abs` can be encoded as a source modifier bit on an instruction that reads the value — similar to how `neg` works — but only when the `abs` is the direct input to another ALU instruction that supports the modifier. When `abs(expr)` is used as a standalone expression (assigned to a variable, returned, or passed to a function), and the compiler cannot fold it into the consuming instruction, it must emit an explicit instruction. The typical lowering is `v_max_f32 dst, src, -src` (which computes `max(x, -x)`), occupying a full VALU slot.

On NVIDIA Turing and Ada Lovelace, the equivalent is a real FP32 instruction rather than a free modifier in all cases where the absolute value is not directly consumed by a fused operation. On Intel Xe-HPG, `abs` similarly requires a dedicated instruction when it cannot be absorbed into the source modifier of the next op. In all cases, applying `abs` to a value that is already proven non-negative emits a real op that computes `max(x, -x)` and obtains exactly `x` — the instruction does work that produces no new information.

The three matched patterns are the most common sites in GPU shader code. `x * x` appears in squared-distance calculations, Fresnel terms, and moment shadow map variance computations. `dot(v, v)` appears in squared-length tests and normalization guards. `abs(saturate(x))` appears when authors defensively wrap a saturate result in `abs` to guard against compiler-introduced negative zeros or when code is copied from a context where the input was not clamped. Removing the `abs` in each case eliminates the wasted instruction and, in the `saturate` case, removes a read-after-write dependency that can block instruction scheduling.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/redundant.hlsl, lines 34-37
// HIT(redundant-abs): x*x is non-negative.
float abs_of_square(float x) {
    return abs(x * x);
}

// From tests/fixtures/phase2/redundant.hlsl, lines 39-42
// HIT(redundant-abs): dot(v,v) is non-negative.
float abs_of_dot_self(float3 v) {
    return abs(dot(v, v));
}

// From tests/fixtures/phase2/redundant.hlsl, lines 44-47
// HIT(redundant-abs): saturate output is non-negative.
float abs_of_saturate(float x) {
    return abs(saturate(x));
}
```

### Good

```hlsl
// After machine-applicable fix — abs dropped in each case:
float abs_of_square(float x) {
    return x * x;
}

float abs_of_dot_self(float3 v) {
    return dot(v, v);
}

float abs_of_saturate(float x) {
    return saturate(x);
}
```

## Options

none

## Fix availability

**machine-applicable** — Dropping `abs` from any of the three matched patterns is a pure textual substitution with no observable semantic change. For `x * x`: IEEE 754 guarantees that a finite float squared is non-negative (negative zero squares to positive zero). For `dot(v, v)`: the result is a sum of squares, always non-negative. For `saturate(expr)`: the output is clamped to [0, 1]. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [redundant-saturate](redundant-saturate.md) — detects `saturate(saturate(x))` and is related because `abs(saturate(x))` is another form of redundant wrapping
- Related rule: [clamp01-to-saturate](clamp01-to-saturate.md) — replaces `clamp(x, 0.0, 1.0)` with `saturate(x)`, after which this rule may fire on the `abs` wrapper
- HLSL intrinsic reference: `abs`, `saturate`, `dot` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [saturate-redundancy overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-abs.md)

*© 2026 NelCit, CC-BY-4.0.*
