---
id: comparison-with-nan-literal
category: misc
severity: error
applicability: machine-applicable
since-version: "v0.2.0"
phase: 2
---

# comparison-with-nan-literal

> **Pre-v0 status:** this rule page documents a planned diagnostic. Behaviour and options are subject to change before the first stable release.

## What it detects

Any comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`) whose left or right operand is a literal NaN-producing expression. The canonical forms are `0.0 / 0.0` (zero divided by zero), `sqrt(-1)` or `sqrt(-1.0)` (square root of a negative literal), and any constant-foldable expression that the front-end can statically resolve to a NaN bit pattern. The rule fires on the comparison node, not on the NaN-producing sub-expression in isolation.

## Why it matters on a GPU

IEEE 754 mandates that every ordered comparison involving a NaN operand returns `false`, and every unordered comparison returns `true`. This means `x < (0.0 / 0.0)` is unconditionally `false` for all finite, infinite, and NaN values of `x` — including when `x` is itself NaN. The comparison is a constant, not a runtime test.

When the NaN literal is written inline, a sufficiently advanced compiler can constant-fold the branch away. In practice, DXIL-targeting compilers (DXC, FXC) and SPIR-V-targeting compilers do not always propagate NaN constants across function boundaries or through common-subexpression elimination passes. The result is live conditional instructions — a branch or a `select` — that consume real ALU cycles and, on wave-based architectures (RDNA, Turing), may cause genuine lane divergence for code that can never actually diverge. On RDNA 3, a `v_cmpx_lt_f32` that always produces the same predicate still consumes an SALU slot and modifies `EXEC`, stalling subsequent VALU instructions by one or more cycles.

The correctness risk is equal to the performance risk: code written to detect NaN via `x == (0.0 / 0.0)` will never detect anything, silently allowing NaN values to propagate into downstream computations (texture coordinates, lighting accumulation, depth writes) where they produce corrupted output that is extremely difficult to diagnose.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/math.hlsl — HIT(comparison-with-nan-literal)
bool nan_literal_compare(float x) {
    // HIT(comparison-with-nan-literal): comparison with NaN is always false.
    return x < (0.0 / 0.0);
}

// Also triggers: sqrt of a negative literal constant
bool bad_nan_check(float x) {
    return x == sqrt(-1.0);  // always false
}

// Also triggers: intent was "is x NaN?" but the test is backwards
bool is_nan_attempt(float x) {
    return x == (0.0 / 0.0);  // always false, even when x is NaN
}
```

### Good

```hlsl
// HLSL does not expose isnan() on all SM 5.0 targets; the portable idiom
// exploits the fact that NaN is the only value not equal to itself:
bool is_nan(float x) {
    return x != x;
}

// After machine-applicable fix, dead NaN-literal comparisons are removed
// entirely; the surrounding logic must be restructured to express intent:
bool nan_literal_compare_fixed(float x) {
    // Comparison against NaN was dead code — remove the branch.
    return false;
}
```

## Options

none — this rule has no configurable thresholds. To silence it on a specific call site, use inline suppression:

```hlsl
// hlsl-clippy: allow(comparison-with-nan-literal)
return x < (0.0 / 0.0);
```

To silence it project-wide, add to `.hlsl-clippy.toml`:

```toml
[rules]
comparison-with-nan-literal = "allow"
```

## Fix availability

**machine-applicable** — When the comparison is provably constant-false (ordered comparison against a NaN literal), `hlsl-clippy fix` replaces the entire comparison expression with `false`. When the comparison is provably constant-true (unordered comparison), it is replaced with `true`. The surrounding control flow is not restructured automatically; dead branches produced by this substitution are candidates for the `dead-code-branch` rule (Phase 4).

## See also

- Related rule: [compare-equal-float](compare-equal-float.md) — flags `==` and `!=` on `float`/`half` operands more broadly, including cases where neither operand is a literal NaN
- Phase 4 numerical-safety pack: [acos-without-saturate](acos-without-saturate.md), [div-without-epsilon](div-without-epsilon.md), [sqrt-of-potentially-negative](sqrt-of-potentially-negative.md)
- Companion blog post: _not yet published — will appear alongside the v0.2.0 release_

---

*© 2026 NelCit, CC-BY-4.0.*

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/comparison-with-nan-literal.md)
