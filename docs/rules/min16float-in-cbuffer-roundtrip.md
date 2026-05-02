---
id: min16float-in-cbuffer-roundtrip
category: packed-math
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# min16float-in-cbuffer-roundtrip

> **Pre-v0 status:** this rule is documented ahead of implementation. Detection
> requires tracking the declared type of a cbuffer field through an explicit
> cast to `min16float`; feasible via Slang reflection combined with AST
> pattern matching.

## What it detects

A `min16float` (or `half`) cast applied to a `float` field loaded from a
`cbuffer`. The cbuffer field is declared as 32-bit `float`; the cast
expression `(min16float)CbField` or `min16float(CbField)` performs a 32-to-16
demotion on every read. The rule fires when this pattern appears in a function
body that is called repeatedly (in a loop, in a pixel shader, or in a compute
shader hot path), because the 32-to-16 conversion is paid on every invocation
rather than being absorbed into a one-time constant promotion.

## Why it matters on a GPU

`cbuffer` (constant buffer) fields are always stored as 32-bit aligned types
on the GPU. When a shader reads a `float` cbuffer field and casts it to
`min16float`, the compiler emits a `v_cvt_f16_f32` (RDNA) or `F2FP` (Turing)
conversion instruction on every execution of that load. For a pixel shader
invoked millions of times per frame — or a compute shader across thousands of
thread groups — this single conversion instruction is replicated across every
wave. On RDNA 3, `v_cvt_f16_f32` costs one VALU cycle, which is not itself
expensive, but when the field is accessed inside a loop the instruction is
issued once per iteration per wave, and the ALU time accumulates.

The intended use case for `min16float` is for values that are genuinely
half-precision at the source: texture samples, interpolated vertex attributes,
or values computed by a previous FP16 shader stage. Casting a full-precision
cbuffer constant down to FP16 at every read also discards precision that was
available for free: if the application stored the value as `float` in the
cbuffer, the GPU fetched 32 bits over the constant-buffer bus and then
immediately threw away the low 16 bits of mantissa. The right fix is either
to use the value as `float` throughout (preserving full precision), or to
declare the cbuffer field as `float16_t` / `min16float` if SM 6.2+ is
available and the pipeline is configured with native FP16 support, so the
32-to-16 conversion is done once by the CPU when writing the cbuffer, not by
every GPU invocation.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/packed_math.hlsl — HIT(min16float-in-cbuffer-roundtrip)
cbuffer MinMaxCB {
    float FloatField;    // 32-bit cbuffer entry
};

// Every call to get_as_half() emits a v_cvt_f16_f32 instruction.
min16float get_as_half() {
    return (min16float)FloatField;  // HIT: 32->16 demotion on every read
}
```

### Good

```hlsl
// Option 1: use the value as float — no demotion, full precision.
float get_as_float() {
    return FloatField;
}

// Option 2 (SM 6.2+ / native FP16 pipeline): declare the cbuffer field as
// float16_t so the CPU writes 16-bit data and the GPU reads it without conversion.
cbuffer MinMaxCB16 {
    float16_t FloatField16;   // 16-bit field; no on-GPU conversion needed
};
min16float get_native_half() {
    return (min16float)FloatField16;  // read 16-bit value, no demotion
}

// Option 3: convert once at the call site when entering a min16float context,
// not inside a repeatedly-called helper function.
void ps_entry(float2 uv : TEXCOORD0) {
    min16float scale = (min16float)FloatField;   // one conversion per invocation
    // ... use scale in min16float arithmetic ...
}
```

## Options

none

## Fix availability

**suggestion** — The rule offers two candidate fixes: use the field as `float`
(removing the demotion), or change the cbuffer declaration to `float16_t`.
Either requires verifying that the precision change is acceptable; `hlsl-clippy
fix` presents the options and requests a choice.

## See also

- Related rule: [min16float-opportunity](min16float-opportunity.md) —
  identifying where min16float is beneficial
- Related rule: [manual-f32tof16](manual-f32tof16.md) — hand-rolled conversion
  instead of intrinsic
- HLSL reference: `min16float`, `float16_t`, SM 6.2 native 16-bit types, and
  cbuffer layout rules in DirectX HLSL documentation
- Companion blog post: [packed-math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/min16float-in-cbuffer-roundtrip.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
