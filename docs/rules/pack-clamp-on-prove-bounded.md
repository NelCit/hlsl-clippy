---
id: pack-clamp-on-prove-bounded
category: packed-math
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# pack-clamp-on-prove-bounded

> **Pre-v0 status:** this rule is documented ahead of implementation. Proving
> that a value is in [0, 255] requires range propagation through the IR and
> is not yet wired into the linter pipeline.

## What it detects

A call to `pack_clamp_u8` (or `pack_clamp_s8`) where the argument can be
proven to already lie within the clamped range. Specifically: when the
argument is the result of `(uint4)(saturate(v) * 255.0)` or an equivalent
expression that passes through `saturate`, `clamp(..., 0, 1)`, or a
`min`/`max` pair bounding the value to [0, 1] before scaling to [0, 255], the
clamping inside `pack_clamp_u8` is provably a no-op. The rule fires when value
range analysis can establish that each component of the `uint4` argument is
in [0, 255] on all execution paths.

## Why it matters on a GPU

`pack_clamp_u8` includes an implicit per-component clamp before packing. On
RDNA, this maps to `v_cvt_pk_u8_f32` with the clamp modifier set, or to an
extra `v_min_u32`/`v_max_u32` pair applied to the integer operands before the
byte-packing instruction. On Turing, it maps to `VMIN`/`VMAX` instructions
preceding `PRMT`. When the inputs are already bounded, these clamp instructions
execute and consume ALU slots while producing a result identical to what
`pack_u8` would produce without the clamp. Replacing `pack_clamp_u8` with
`pack_u8` in the bounded case eliminates one instruction per component (up to
four instructions removed for a `uint4` argument).

In a pixel shader invoked at full-screen 4K resolution — roughly 8 million
pixels at 2160p — each removed instruction saves one VALU issue per 64 pixels
(one wave), totalling roughly 125 000 instructions per frame for a single
`pack_clamp_u8` call in the hot path. The individual saving is small, but
packing operations appear frequently in tone-mapping, colour-grading, and
post-processing passes that are already ALU-bound, and the compiler typically
does not perform this optimisation because it requires inter-intrinsic value
range analysis that the HLSL compiler does not implement for user-visible
packing intrinsics.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/packed_math.hlsl — HIT(pack-clamp-on-prove-bounded)
uint pack_clamped_saturated(float4 col) {
    float4 s = saturate(col);               // provably in [0, 1]
    uint4  u = (uint4)(s * 255.0 + 0.5);   // provably in [0, 255]
    return pack_clamp_u8(u);               // HIT: clamp is a provable no-op
}
```

### Good

```hlsl
// Use pack_u8 when inputs are proven bounded — same result, one fewer clamp.
uint pack_clamped_saturated(float4 col) {
    float4 s = saturate(col);
    uint4  u = (uint4)(s * 255.0 + 0.5);
    return pack_u8(u);   // no-clamp version; correct because u in [0, 255]
}

// When inputs are NOT proven bounded — keep pack_clamp_u8.
uint pack_unbounded(float4 col) {
    uint4 u = (uint4)(col * 255.0);
    return pack_clamp_u8(u);   // correct: col may be outside [0, 1]
}
```

## Options

none

## Fix availability

**suggestion** — Replacing `pack_clamp_u8` with `pack_u8` is safe only when
the value-range proof holds on all execution paths. The rule shows the proof
and proposes the change; `hlsl-clippy fix` applies it after the user confirms
the range assumption is correct.

## See also

- Related rule: [pack-then-unpack-roundtrip](pack-then-unpack-roundtrip.md) —
  dead round-trip in packing
- Related rule: [unpack-then-repack](unpack-then-repack.md) — repack without
  modification
- HLSL reference: `pack_u8`, `pack_clamp_u8`, `pack_s8`, `pack_clamp_s8` —
  SM 6.6 packing intrinsics
- Companion blog post: [packed-math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pack-clamp-on-prove-bounded.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
