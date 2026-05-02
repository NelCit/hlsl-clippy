---
id: pack-then-unpack-roundtrip
category: packed-math
severity: warn
applicability: machine-applicable
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl"]
---

# pack-then-unpack-roundtrip

> **Pre-v0 status:** this rule is documented ahead of implementation. Pattern
> detection requires modelling the SM 6.6 packing intrinsics in the rule
> engine's expression-tree matcher.

## What it detects

A `pack_u8(unpack_u8u32(x))` sequence — or the signed equivalents
`pack_s8(unpack_s8s32(x))` — where no arithmetic is performed on the
individual channels between the unpack and the repack. The result of the pair
is always equal to the original packed value `x`; both operations are dead.
The rule also fires on the floating-point analogue: `f16tof32(f32tof16(x))`
where the intermediate 16-bit value is not used for any other purpose, because
the result of the pair is the FP32 value nearest to `x` that is representable
in FP16 — a precision loss that serves no purpose if the output is used in a
full-precision context.

## Why it matters on a GPU

Both `pack_u8`/`unpack_u8u32` and `f32tof16`/`f16tof32` are ALU instructions.
Each costs at least one VALU cycle on RDNA 3 and Turing; the pair together
cost two cycles plus a data dependency that prevents the subsequent use of the
result from issuing until both complete. In a compute shader that processes
large arrays of packed values, a dead round-trip in the inner loop adds two
wasted cycles per element per wave. At 32 million elements across a 1080p
frame buffer and 64 lanes per wave, the dead pair contributes roughly 1 million
wasted VALU instructions per dispatch — a measurable fraction of a 60 Hz
frame budget.

The `f32tof16`/`f16tof32` round-trip has a secondary correctness implication:
the output is not bitwise equal to the input for any value whose FP32
representation is not exactly representable in FP16. If the intent was to
quantise `x` to FP16 precision intentionally (for example, to match the
quantisation of a stored FP16 texture), the round-trip is meaningful and the
rule should be suppressed. In all other cases it is a silent precision loss
with no performance benefit.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/packed_math.hlsl — HIT(pack-then-unpack-roundtrip)

uint pack_unpack_roundtrip(uint packed) {
    uint4 unpacked = unpack_u8u32(packed);
    return pack_u8(unpacked);   // HIT: result == packed; both ops are dead
}

float f16_roundtrip(float x) {
    uint h = f32tof16(x);
    return f16tof32(h);         // HIT: result == nearest-FP16(x); dead if not intended
}
```

### Good

```hlsl
// Pass the original value through unchanged.
uint pack_passthrough(uint packed) {
    return packed;
}

float f_passthrough(float x) {
    return x;
}

// When modification is present — not a round-trip, rule does not fire.
uint pack_with_modify(uint packed) {
    uint4 channels = unpack_u8u32(packed);
    channels.r = channels.r >> 1u;
    return pack_u8(channels);
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing `pack_u8(unpack_u8u32(x))` with `x` is a
pure textual substitution with no observable semantic change for any input. The
`f16tof32(f32tof16(x))` fix is also machine-applicable when the caller does
not depend on the precision-reducing effect of the round-trip; the rule emits
a note about the precision change and applies the fix automatically.
`hlsl-clippy fix` applies both without human confirmation.

## See also

- Related rule: [unpack-then-repack](unpack-then-repack.md) — the same
  pattern from the unpacking direction
- Related rule: [pack-clamp-on-prove-bounded](pack-clamp-on-prove-bounded.md) —
  redundant clamping in packing
- HLSL reference: `pack_u8`, `unpack_u8u32`, `f32tof16`, `f16tof32` — SM 6.6
  packing intrinsics in the DirectX HLSL Shader Model 6.6 documentation
- Companion blog post: [packed-math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pack-then-unpack-roundtrip.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
