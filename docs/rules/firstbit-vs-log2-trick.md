---
id: firstbit-vs-log2-trick
category: math
severity: warn
applicability: machine-applicable
since-version: v0.2.0
phase: 2
---

# firstbit-vs-log2-trick

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Expressions that compute the position of the most-significant set bit of a non-zero unsigned integer using a `log2` / float-cast detour, in any of the common shapes: `(uint)log2((float)x)`, `(int)log2((float)x)`, `floor(log2((float)x))`, or the asfloat-asuint exponent-extraction trick `(asuint((float)x) >> 23) - 127`. The rule also matches the symmetric low-bit form built around `log2` of `x & -x`. The match keys on a `log2` (or `asuint` exponent extraction) applied to a value that has been freshly cast or coerced from an unsigned integer, with the result then truncated back to an integer. It does not fire when the surrounding code uses the floating-point logarithm value itself for anything other than re-truncation.

## Why it matters on a GPU

`firstbithigh(uint)` and `firstbitlow(uint)` are both single ISA instructions on every shader-capable GPU since SM 5.0. AMD RDNA 3 lowers them to `v_ffbh_u32` and `v_ffbl_b32`, both full-rate VALU integer ops. NVIDIA Turing and Ada Lovelace use `FLO` (find-leading-one) and a related instruction; both retire in a single cycle on the integer pipe. The `log2` detour, by contrast, requires three steps: an integer-to-float conversion (`v_cvt_f32_u32` on RDNA, full-rate but with a separate ALU port), a `log2` (`v_log_f32` on RDNA 3, which issues at one-quarter rate as a transcendental on the SFU/MUFU shared with `exp`, `rcp`, `rsqrt`, `sin`, and `cos`), and a float-to-integer truncation back. The end-to-end cost is roughly 6-8 VALU-equivalent cycles versus one for the intrinsic — and the slow step is exactly the transcendental unit that the rest of the shader is also competing for.

The asfloat-bit-trick variant skips the transcendental but is silently wrong for inputs that do not round-trip exactly through FP32. Any `x` above 2^24 loses low-bit precision in the cast, so `firstbithigh` of, say, `0x01000001u` returns 24 (correct) while `(uint)log2((float)0x01000001u)` returns 24 only because the literal happens to round down — change the low bits and the answer drifts. The exponent-bias trick (`(asuint((float)x) >> 23) - 127`) has the same precision ceiling and additionally returns garbage for `x == 0` because `log2(0)` is `-inf` and the cast truncation is undefined. `firstbithigh` is defined to return `0xFFFFFFFFu` for a zero input, which is a checkable sentinel; the float trick produces silently corrupt indices.

The pattern is most common in light-clustering and tile-binning code that walks a per-tile bitmask of intersecting lights, in BVH traversal stacks that pack node indices, and in occupancy/empty-tile compaction that finds the lowest free slot in a bitset. These are inner-loop hot paths where the difference between a one-cycle integer op and a quarter-rate transcendental, executed thousands of times per frame, is directly measurable in shader nanoseconds and percent of frame time.

## Examples

### Bad

```hlsl
// log2 detour: float conversion + transcendental + truncation. ~6-8 cycles.
uint highest_set_bit(uint x) {
    return (uint)log2((float)x);
}

// asuint exponent trick: precision-limited above 2^24, undefined at zero.
uint highest_set_bit_bits(uint x) {
    return (asuint((float)x) >> 23u) - 127u;
}
```

### Good

```hlsl
// Single VALU integer instruction; defined behaviour for x == 0.
uint highest_set_bit(uint x) {
    return firstbithigh(x);
}
```

## Options

none

## Fix availability

**machine-applicable** — For the canonical `(uint)log2((float)x)` form (and its `(int)` and `floor()` variants), the fix is a direct substitution to `firstbithigh(x)` with strictly greater precision and identical results for all `x` in the float-representable range. `hlsl-clippy fix` applies it automatically. The asuint exponent-bias variant is rewritten the same way; the intrinsic's defined zero-input sentinel is a behaviour improvement, not a regression.

## See also

- Related rule: [countbits-vs-manual-popcount](countbits-vs-manual-popcount.md) — pairs with this rule as the bit-counting companion
- Related rule: [inv-sqrt-to-rsqrt](inv-sqrt-to-rsqrt.md) — same transcendental-unit argument applied to reciprocal square root
- HLSL intrinsic reference: `firstbithigh`, `firstbitlow` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/firstbit-vs-log2-trick.md)

*© 2026 NelCit, CC-BY-4.0.*
