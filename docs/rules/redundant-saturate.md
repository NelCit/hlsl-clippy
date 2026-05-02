---
id: redundant-saturate
category: saturate-redundancy
severity: warn
applicability: machine-applicable
since-version: v0.2.0
phase: 2
---

# redundant-saturate

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Calls of the form `saturate(saturate(x))` where the outer `saturate` is applied to an expression already guaranteed to be in [0, 1] because it is itself a `saturate` call. The rule matches both the direct nested form — `saturate(saturate(expr))` — and the split-variable form where a `saturate` result is stored in an intermediate variable and then passed to a second `saturate` (see lines 8-11 of `tests/fixtures/phase2/redundant.hlsl`). It does not fire when the argument to the outer `saturate` could originate from any source other than a prior `saturate`.

## Why it matters on a GPU

On AMD RDNA, RDNA 2, and RDNA 3 hardware, `saturate` is not an independent instruction. It is an output modifier bit (`_clamp`) that is folded into whichever ALU instruction produces the value — an ADD, MUL, MAD, FMA, or similar — at zero additional cycle cost. The compiler can attach `_clamp` to the last instruction that writes the register and the hardware enforces the [0, 1] clamp during writeback with no extra cycles.

When the source is `saturate(saturate(x))`, the inner `saturate` can still be lowered to a free `_clamp` modifier on the instruction that produces `x`. The outer `saturate`, however, cannot be folded into the same instruction because its input is already a distinct value in a VGPR. The compiler must emit a separate real ALU instruction — typically `v_max_f32` followed by `v_min_f32`, or a `v_med3_f32` — to clamp the already-clamped value. On NVIDIA Turing and Ada Lovelace the situation is analogous: `.sat` is an instruction modifier, but a second `.sat` applied to the result of a previous `.sat` cannot share the same modifier slot and therefore materialises as a real FP32 clamp sequence. On Intel Xe-HPG the pattern similarly costs one extra ALU operation.

The fix eliminates the outer call entirely, reducing a two-instruction clamp sequence (or one wasted ALU op) to the zero-cost output modifier the inner `saturate` already provides. In pixel shaders that process HDR accumulation buffers and tone-map per channel — where `saturate` is called at multiple levels of a call graph — the redundant forms accumulate into a measurable instruction count increase.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/redundant.hlsl, line 3-6
// HIT(redundant-saturate): saturate is idempotent.
float3 nested_saturate(float3 c) {
    return saturate(saturate(c));
}

// From tests/fixtures/phase2/redundant.hlsl, lines 8-11
// HIT(redundant-saturate): saturate of an already-saturated value.
float3 nested_saturate_split(float3 c) {
    float3 a = saturate(c);
    return saturate(a);
}
```

### Good

```hlsl
// After machine-applicable fix — outer saturate dropped:
float3 nested_saturate(float3 c) {
    return saturate(c);
}

float3 nested_saturate_split(float3 c) {
    float3 a = saturate(c);
    return a;
}
```

## Options

none

## Fix availability

**machine-applicable** — The fix is a pure textual substitution with no observable semantic change. `saturate` is idempotent: for any `x`, `saturate(saturate(x)) == saturate(x)`. `hlsl-clippy fix` applies it automatically.

## See also

- Related rule: [clamp01-to-saturate](clamp01-to-saturate.md) — replaces `clamp(x, 0.0, 1.0)` with `saturate(x)`
- Related rule: [redundant-abs](redundant-abs.md) — drops `abs` around expressions proven non-negative, including `saturate` output
- HLSL intrinsic reference: `saturate` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [saturate-redundancy overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-saturate.md)

*© 2026 NelCit, CC-BY-4.0.*
