---
id: redundant-precision-cast
category: misc
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# redundant-precision-cast

> **Pre-v0 status:** this rule page documents a planned diagnostic. Behaviour and options are subject to change before the first stable release.

## What it detects

Nested cast expressions that form precision-degrading or no-op round-trips. Three specific patterns are detected:

1. **`float → int → float`**: `(float)((int)x)` where `x` is already of type `float` or `half`. The inner `(int)` truncates toward zero, silently discarding the fractional part of `x`; the outer `(float)` re-widens to float. The result is `trunc(x)` — but written in a way that obscures the truncation.
2. **`half → float → half`** (or any narrowing followed by an immediate widening to the same width): `(half)((float)h)` where `h` is already `half`. The inner `(float)` is a no-op promotion; the outer `(half)` is a no-op demotion back to the original precision. The round-trip neither gains nor loses precision and emits two conversion instructions for zero net effect.
3. **`int → float → int`**: `(int)((float)i)` where `i` is an integer type. The inner `(float)` may lose precision for large integers (greater than 2^23 for `float`); the outer `(int)` truncates back. For values that fit in 23 bits the optimizer may fold this, but it cannot reliably prove range and so the round-trip often survives to codegen.

The rule fires on the outer cast node when it can statically determine the type of the innermost operand and the intervening cast creates a type that is no wider (in the case of int→float) or is immediately narrowed back (in the half→float→half case).

## Why it matters on a GPU

Each type conversion — `v_cvt_f32_i32`, `v_cvt_i32_f32`, `v_cvt_f32_f16`, `v_cvt_f16_f32` on RDNA; the equivalent `FCONV`/`I2F`/`F2I` family on Turing and Xe-HPG — is a real ALU instruction. Pairs of such instructions in a round-trip pattern consume two instruction-issue slots and two VGPR reads/writes. On RDNA 3, conversion instructions execute in the VALU pipeline at full throughput, so a two-instruction round-trip costs two cycles of VALU occupancy per lane — identical in cost to two FP32 multiplies — for zero arithmetic progress.

The correctness hazard is more significant than the performance cost. The `float → int → float` pattern (pattern 1) is the most dangerous: it is visually similar to a no-op cast, but it silently truncates the fractional part. Code written as `(float)((int)x)` when `trunc(x)` or `floor(x)` was intended is correct by accident; code written as `(float)((int)x)` when the author expected no data loss is a silent bug. This pattern appears in shader ports from integer-arithmetic contexts (index computation, bit manipulation) where the intermediate `(int)` was meaningful but the outer `(float)` was added carelessly to satisfy a type mismatch.

The `int → float → int` pattern (pattern 3) introduces a precision hazard for integers larger than 16777216 (2^23), where `float` cannot represent consecutive integer values. On architectures that use `float` as an intermediate for integer arithmetic — a pattern found in some GLSL-to-HLSL ports — this can silently round large counters or indices to the nearest representable float, corrupting array access patterns.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/math.hlsl — HIT(redundant-precision-cast)
float redundant_precision_round_trip(float x) {
    // HIT(redundant-precision-cast): float → int → float drops fraction silently.
    return (float)((int)x);
}

// half → float → half: no-op round-trip, two conversion instructions wasted
half no_op_round_trip(half h) {
    return (half)((float)h);  // HIT(redundant-precision-cast)
}

// int → float → int: precision hazard for values > 2^23
int int_round_trip(int i) {
    return (int)((float)i);  // HIT(redundant-precision-cast)
}
```

### Good

```hlsl
// If truncation was intended, make it explicit:
float explicit_truncation(float x) {
    return trunc(x);   // unambiguous; machine-applicable fix target
}

// If the half cast chain was a no-op, remove it entirely:
half no_op_fixed(half h) {
    return h;
}

// If the int round-trip was unintentional, remove the float cast:
int int_round_trip_fixed(int i) {
    return i;
}

// If float arithmetic on an integer was intentional, make the boundary explicit:
float int_to_float_intentional(int i) {
    return (float)i;   // single cast, clear intent, no round-trip
}
```

## Options

none — this rule has no configurable thresholds. To silence it on a specific call site, use inline suppression:

```hlsl
// hlsl-clippy: allow(redundant-precision-cast)
return (float)((int)x);
```

To silence it project-wide, add to `.hlsl-clippy.toml`:

```toml
[rules]
redundant-precision-cast = "allow"
```

## Fix availability

**machine-applicable** — For the `float → int → float` pattern, `hlsl-clippy fix` replaces `(float)((int)x)` with `trunc(x)`, which is semantically identical and makes the truncation explicit. For the `half → float → half` no-op, it removes both casts and retains the inner expression. For the `int → float → int` pattern, it removes both casts and retains the inner expression when the round-trip is provably a no-op; when precision loss is possible, it emits a suggestion instead of an automatic fix.

## See also

- Related rule: [compare-equal-float](compare-equal-float.md) — flags `==` and `!=` on `float`/`half`; often co-occurs with redundant cast patterns
- Related rule: [comparison-with-nan-literal](comparison-with-nan-literal.md) — NaN produced by float arithmetic after a silent truncation is a common source of downstream NaN literal comparisons
- Phase 4 numerical-safety pack: [acos-without-saturate](acos-without-saturate.md), [div-without-epsilon](div-without-epsilon.md), [sqrt-of-potentially-negative](sqrt-of-potentially-negative.md)
- Companion blog post: _not yet published — will appear alongside the v0.2.0 release_

---

*© 2026 NelCit, CC-BY-4.0.*

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/redundant-precision-cast.md)
