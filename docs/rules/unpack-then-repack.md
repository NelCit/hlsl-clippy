---
id: unpack-then-repack
category: packed-math
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# unpack-then-repack

> **Pre-v0 status:** this rule is documented ahead of implementation. Pattern
> detection is feasible at the AST or early-IR level once the SM 6.6 packing
> intrinsics are modelled in the linter's rule engine.

## What it detects

An `unpack_u8u32` (or `unpack_s8s32`) call on a value whose four unpacked
channels are then repacked with `pack_u8` (or `pack_s8`) without any
arithmetic modification to the individual channels in between. The rule also
fires on the floating-point analogue: an `f32tof16` conversion immediately
followed by `f16tof32` on the same lane, with no operation on the 16-bit
integer value between the two calls. In both cases the round-trip is a no-op
and both operations can be eliminated: the original packed value is equal to
the result of the repack.

## Why it matters on a GPU

`unpack_u8u32` and `pack_u8` are ALU instructions: on RDNA, they map to
`v_cvt_pk_u8_f32` and related instructions; on Turing they map to
`PRMT`/`BFE`/`BFI` sequences. While individually cheap, the unpack-repack
round-trip occupies two ALU instructions per component and introduces a
data-flow dependency chain that prevents the scheduler from issuing the
surrounding instructions in parallel. For a shader that processes packed
colour data in a tight loop, eliminating the dead round-trips reduces the
issue-slot pressure and shrinks the number of live intermediates.

The `f32tof16`/`f16tof32` pair is similarly wasteful. `f32tof16` is a
conversion instruction that truncates the mantissa and biases the exponent;
`f16tof32` reverses it. The pair together produce the FP32 value closest to
the original that is representable in FP16 — a precision-reducing no-op if
the result is used in a context where full FP32 precision was already
available. On Turing, `F2FP` and `HADD2` instructions handle these
conversions; each takes one ALU cycle. Eliminating the pair saves two
instructions per site and removes the intermediate `uint` SSA value that
temporarily holds the packed bits.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/packed_math.hlsl — HIT(pack-then-unpack-roundtrip)

// 8888 round-trip: result equals PackedInput.
uint pack_unpack_roundtrip(uint packed) {
    uint4 unpacked = unpack_u8u32(packed);
    return pack_u8(unpacked);  // HIT: no modification between unpack and repack
}

// f16 round-trip: result equals x (modulo FP16 precision loss).
float f16_roundtrip(float x) {
    uint h = f32tof16(x);
    return f16tof32(h);  // HIT: conversion pair is a precision-reducing no-op
}
```

### Good

```hlsl
// If the goal is to pass the value through unchanged, use it directly.
uint pack_passthrough(uint packed) {
    return packed;
}

float f_passthrough(float x) {
    return x;
}

// When the channels ARE modified — the round-trip is intentional.
uint pack_with_modify(uint packed) {
    uint4 channels = unpack_u8u32(packed);
    channels.r = channels.r >> 1u;   // half the red channel
    return pack_u8(channels);        // not a round-trip; rule does not fire
}
```

## Options

none

## Fix availability

**suggestion** — The fix is to replace the round-trip pair with the original
value. For `unpack`/`repack` this is machine-applicable when no other use of
the unpacked channels exists. For `f32tof16`/`f16tof32` the fix removes
precision that was being explicitly thrown away; a human should confirm the
original precision was not needed. `hlsl-clippy fix` proposes the simplification
and requests confirmation before applying.

## See also

- Related rule: [pack-then-unpack-roundtrip](pack-then-unpack-roundtrip.md) —
  the same pattern framed from the pack side
- Related rule: [pack-clamp-on-prove-bounded](pack-clamp-on-prove-bounded.md) —
  redundant clamp in packing path
- Related rule: [manual-f32tof16](manual-f32tof16.md) — hand-rolled FP16
  conversion
- HLSL intrinsic reference: `pack_u8`, `unpack_u8u32`, `f32tof16`, `f16tof32`
  in DirectX HLSL SM 6.6 documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/unpack-then-repack.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
