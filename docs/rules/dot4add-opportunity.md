---
id: dot4add-opportunity
category: math
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# dot4add-opportunity

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A four-tap integer dot product computed manually by unpacking byte-packed
values with shifts and masks, multiplying the individual bytes, and summing
the products — the classic `(a >> 0) & 0xFF) * ((b >> 0) & 0xFF) + ...`
pattern for all four byte lanes. The rule fires when all four lanes of two
packed `uint` operands are multiplied and accumulated, and the result is a
`uint` or `int` accumulation, matching the semantics of `dot4add_u8packed`
(SM 6.4) or `dot4add_i8packed`. It also fires on variants where the shift and
mask order differs but the mathematical equivalence holds.

## Why it matters on a GPU

`dot4add_u8packed` and `dot4add_i8packed` are single instructions on hardware
that supports SM 6.4 (DirectX 12 Ultimate): they map to `DP4a` on NVIDIA
Turing+, and to `v_dot4_u32_u8` / `v_dot4_i32_i8` on AMD RDNA 2+. The
`DP4a` instruction takes two packed 32-bit operands (four `uint8` or `int8`
values per operand), computes the four-lane integer dot product, and
accumulates the result into a 32-bit integer — all in one clock cycle on the
integer ALU. The unrolled manual implementation requires 8 mask operations,
4 shift operations, 4 multiplies, and 3 additions for a total of 19 ALU
instructions, each taking one integer ALU cycle on RDNA and Turing (though
some can dual-issue). The intrinsic replaces all 19 with 1.

In neural-network inference, image processing, and audio DSP shaders that
compute quantised dot products — all of which have become common in GPU compute
workloads — the `dot4add` intrinsic is the primary performance lever for
integer convolutions. On RDNA 2, a single `v_dot4_u32_u8` instruction issues
from the VALU at the same throughput as a single `v_mul_lo_u32`. Replacing 19
instructions with 1 yields an 8-10x throughput improvement for the dot-product
kernel alone, modulo memory bandwidth. The VGPRs freed from the 8 intermediate
byte-extracted values also contribute a small but non-zero reduction in peak
register pressure.

The rule is disabled by default on projects that declare a minimum SM below
6.4, because `dot4add_u8packed` is not available on hardware older than RDNA 2
/ Turing. A per-project option enables or disables the suggestion.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/packed_math.hlsl — HIT(dot4add-opportunity)
uint manual_dot4_u8(uint a_packed, uint b_packed) {
    uint a0 = (a_packed      ) & 0xFFu;
    uint a1 = (a_packed >>  8) & 0xFFu;
    uint a2 = (a_packed >> 16) & 0xFFu;
    uint a3 = (a_packed >> 24) & 0xFFu;
    uint b0 = (b_packed      ) & 0xFFu;
    uint b1 = (b_packed >>  8) & 0xFFu;
    uint b2 = (b_packed >> 16) & 0xFFu;
    uint b3 = (b_packed >> 24) & 0xFFu;
    return a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3;
}
```

### Good

```hlsl
// SM 6.4 intrinsic — one DP4a instruction on Turing+/RDNA2+.
uint fast_dot4_u8(uint a_packed, uint b_packed) {
    return dot4add_u8packed(a_packed, b_packed, 0u);
}

// With accumulation into an existing sum.
uint fast_dot4_accumulate(uint a, uint b, uint acc) {
    return dot4add_u8packed(a, b, acc);
}
```

## Options

- `require-sm` (string, default: `"6.4"`) — minimum shader model required to
  suggest `dot4add_u8packed`. Set to `"6.0"` to suppress on platforms that
  predate SM 6.4 support. Set to `"none"` to suppress the rule entirely.

## Fix availability

**suggestion** — The rewrite to `dot4add_u8packed` is correct only when the
accumulator initial value is zero (or when it is the `acc` parameter of a
multi-tap accumulation). The rule proposes the substitution and requests
confirmation; `hlsl-clippy fix` applies it with the accumulator set to `0u`
unless the surrounding code already provides one.

## See also

- Related rule: [min16float-opportunity](min16float-opportunity.md) — FP16
  packed arithmetic for float ALU-bound shaders
- HLSL intrinsic reference: `dot4add_u8packed`, `dot4add_i8packed` —
  SM 6.4 integer dot-product-accumulate intrinsics
- DirectX HLSL Shader Model 6.4 specification — integer dot products
- Companion blog post: [packed-math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/dot4add-opportunity.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
