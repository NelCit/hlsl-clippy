---
id: compare-equal-float
category: misc
severity: warn
applicability: suggestion
since-version: "v0.2.0"
phase: 2
---

# compare-equal-float

> **Pre-v0 status:** this rule page documents a planned diagnostic. Behaviour and options are subject to change before the first stable release.

## What it detects

Any use of `==` or `!=` where both operands are of type `float`, `half`, `float2`/`float3`/`float4`, or the corresponding `half` vector types. The rule fires on the comparison operator node. It does not fire when either operand is of an integer type (`int`, `uint`, `bool`, etc.) or when the comparison is between a float value and a literal that the compiler can prove is exactly representable and the surrounding context is a known-safe pattern (such as comparing against `0.0` inside a `isnan`-style idiom — see Options). Integer `==` is correct and is never flagged.

## Why it matters on a GPU

Floating-point numbers represent values with finite precision: a 32-bit `float` has a 23-bit mantissa, meaning the gap between adjacent representable values (the ULP, unit in the last place) grows with magnitude. Two computations that produce the "same" value by different instruction sequences — different order of operations, different FMA folding, different intermediate registers — can land in adjacent ULPs and compare unequal even when mathematically they should be identical.

On GPU hardware, this fragility is amplified. Different shader stages run on different execution units and may use different rounding modes. Driver shader compilation pipelines are not required to preserve associativity, and compilers for RDNA, Turing, and Xe-HPG routinely reorder multiplications and additions to improve ILP or reduce register pressure. A value that compares equal on one driver version or GPU SKU may not compare equal on another, making float `==` a latent cross-vendor portability hazard as well as a correctness issue.

The industry-standard fix is an epsilon comparison: `abs(a - b) < epsilon`, where `epsilon` is chosen based on the domain (world-space distances have different tolerances than normalised direction vectors). Because the correct epsilon is inherently domain-specific, `hlsl-clippy` cannot generate it automatically — hence `suggestion` applicability. The rule's job is to flag the smell; the programmer supplies the tolerance.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase2/math.hlsl — HIT(compare-equal-float)
bool float_equality(float x) {
    // HIT(compare-equal-float): == on float is NaN-fragile.
    return x == 0.0;
}

// Also triggers on !=
bool not_equal(float a, float b) {
    return a != b;   // HIT(compare-equal-float)
}

// Also triggers on vector components
bool same_direction(float3 a, float3 b) {
    return dot(a, b) == 1.0;  // HIT(compare-equal-float)
}
```

### Good

```hlsl
// Epsilon comparison — tolerance chosen for the domain
static const float kEpsilon = 1e-6;

bool near_zero(float x) {
    return abs(x) < kEpsilon;
}

bool near_equal(float a, float b) {
    return abs(a - b) < kEpsilon;
}

// For unit-vector dot products, a looser tolerance is often appropriate
static const float kCosTolerance = 1e-4;

bool same_direction(float3 a, float3 b) {
    return abs(dot(a, b) - 1.0) < kCosTolerance;
}

// Integer == is fine and is not flagged
bool integer_equal(int x) {
    return x == 0;
}
```

## Options

- `tolerance-required` (bool, default: `false`) — When `false` (default), the rule fires on any float `==` or `!=`, regardless of context. When `true`, the rule still fires unless `hlsl-clippy` can statically detect that the comparison is guarded by an epsilon expression of the form `abs(a - b) < expr` or `distance(a, b) < expr` in the same scope. Setting `true` reduces noise on codebases where epsilon patterns are already established but not consistently applied.

Configure in `.hlsl-clippy.toml`:

```toml
[rules.compare-equal-float]
tolerance-required = true
```

## Fix availability

**suggestion** — `hlsl-clippy fix --suggestion` inserts a placeholder epsilon comparison:

```hlsl
// Suggested fix (epsilon value must be chosen by the programmer):
abs(x - 0.0) < /*epsilon*/1e-6
```

The epsilon literal `1e-6` is a placeholder. The programmer must replace it with a value appropriate for the domain. Because this change is not semantics-preserving without human review, it is never applied automatically by `hlsl-clippy fix` without the `--suggestion` flag.

## See also

- Related rule: [comparison-with-nan-literal](comparison-with-nan-literal.md) — the specific case where one operand is a literal NaN expression; fired at `error` severity
- Phase 4 numerical-safety pack: [acos-without-saturate](acos-without-saturate.md), [div-without-epsilon](div-without-epsilon.md), [sqrt-of-potentially-negative](sqrt-of-potentially-negative.md)
- Companion blog post: _not yet published — will appear alongside the v0.2.0 release_

---

*© 2026 NelCit, CC-BY-4.0.*

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/compare-equal-float.md)
