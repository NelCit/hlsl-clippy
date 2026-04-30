---
id: manual-f32tof16
category: packed-math
severity: warn
applicability: machine-applicable
since-version: v0.7.0
phase: 7
---

# manual-f32tof16

> **Pre-v0 status:** this rule is documented ahead of implementation. The
> bit-pattern matching is feasible at AST level once the rule engine supports
> bitwise-arithmetic pattern templates.

## What it detects

Hand-written bit-twiddling sequences that implement an FP32-to-FP16 or
FP16-to-FP32 conversion manually using `asuint`, `asfloat`, bit-shifts,
masks, and bias additions, rather than calling the `f32tof16` / `f16tof32`
intrinsics (available since SM 5.0) or using a `min16float` cast. The
canonical bad patterns are: extracting the sign bit with `(x >> 31) & 1`,
extracting the exponent with `(x >> 23) & 0xFF`, re-biasing by subtracting
127 and adding 15, masking the mantissa, and assembling the result — all
performed on the raw `uint` bitcast of the `float`. The rule matches both the
full conversion and common sub-idioms that partially re-implement the
intrinsic.

## Why it matters on a GPU

`f32tof16` and `f16tof32` are single-instruction operations on all modern GPU
targets. On AMD RDNA they map to `v_cvt_f16_f32` and `v_cvt_f32_f16`. On
NVIDIA Turing they map to the `F2FP` / `HADD2` conversion path. Each issues
in a single ALU cycle. A hand-rolled implementation performs 8-12 integer
operations: a bitcast, two or three shifts, two or three masks, an arithmetic
operation on the exponent, and a final bitcast back. Even with full pipelining,
that sequence is 8-12 cycles of ALU throughput versus 1. In a compute shader
that packs thousands of FP16 values into a UAV buffer, the difference is
measurable as a fraction of the total dispatch time.

Beyond throughput, the hand-rolled version is almost always incorrect for
denormal numbers, for NaN, and for infinity. `f32tof16` handles these cases
according to the IEEE 754-2008 specification for conversion to binary16;
hand-rolled code typically ignores them, introducing silent NaN propagation or
wrapping to zero in the denormal range. On hardware that runs with
`-fdenorm-flush` enabled (common on AMD and NVIDIA in default compute shader
modes), the behaviour of a manual implementation and the intrinsic may agree
for normal values but diverge for subnormals in ways that are hard to debug.

## Examples

### Bad

```hlsl
// Hand-rolled FP32-to-FP16 — 10 ALU instructions vs. one.
uint manual_f32_to_f16(float v) {
    uint bits  = asuint(v);
    uint sign  = (bits >> 16) & 0x8000u;
    uint exp   = ((bits >> 23) & 0xFFu) - 127u + 15u;
    uint mant  = (bits >> 13) & 0x3FFu;
    return sign | (exp << 10) | mant;
}

// Hand-rolled FP16-to-FP32.
float manual_f16_to_f32(uint h) {
    uint sign = (h & 0x8000u) << 16;
    uint exp  = ((h >> 10) & 0x1Fu) - 15u + 127u;
    uint mant = (h & 0x3FFu) << 13;
    return asfloat(sign | (exp << 23) | mant);
}
```

### Good

```hlsl
// Use the intrinsics — one instruction each, correct for all IEEE cases.
uint good_f32_to_f16(float v) {
    return f32tof16(v);
}

float good_f16_to_f32(uint h) {
    return f16tof32(h);
}

// Or use min16float directly.
min16float good_cast(float v) {
    return (min16float)v;
}
```

## Options

none

## Fix availability

**machine-applicable** — Replacing the hand-rolled sequence with `f32tof16` or
`f16tof32` is a safe textual substitution. The intrinsic is strictly more
correct (handles denormals, NaN, infinity) and the semantic change for normal
values is zero. `hlsl-clippy fix` applies the substitution automatically.

## See also

- Related rule: [unpack-then-repack](unpack-then-repack.md) — `f32tof16`
  immediately followed by `f16tof32` is a precision-reducing no-op
- Related rule: [min16float-opportunity](min16float-opportunity.md) — using
  `min16float` arithmetic instead of manual FP16 packing
- HLSL intrinsic reference: `f32tof16`, `f16tof32` — available from SM 5.0;
  documented in the DirectX HLSL Intrinsics reference
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-f32tof16.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
