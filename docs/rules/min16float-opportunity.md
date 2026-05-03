---
id: min16float-opportunity
category: packed-math
severity: note
applicability: suggestion
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# min16float-opportunity

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

ALU-bound regions in a shader where all values in a computation chain are
`float` (32-bit) but the precision requirement is consistent with `min16float`
(minimum 16-bit): the inputs are either in the normalised range [0, 1] (colour
channels, barycentric weights, UV coordinates), or the computation is an
accumulation whose intermediate error budget allows 16-bit rounding. The rule
fires when it can establish — either from value-range analysis or from explicit
`saturate`/clamp annotations — that no intermediate value in the chain exceeds
the `min16float` representable range and that the output is consumed by a write
to an 8-bit render target, a sampler-feedback write, or an explicit conversion
back to a packed format.

## Why it matters on a GPU

`min16float` maps to FP16 on AMD RDNA and NVIDIA Turing and later. On RDNA 3,
the shader ALU supports native FP16 arithmetic at the same throughput as FP32
in packed form: two FP16 operations can be issued in one FP32 VALU clock using
`v_pk_*_f16` instructions. For a shader that is ALU-limited and operates
predominantly on colour values, converting the inner loop to `min16float` can
double effective VALU throughput without changing the wave count.

The secondary benefit is register pressure. A `min16float4` occupies 2 VGPRs
rather than 4 (each 32-bit VGPR holds two packed FP16 values). Cutting VGPR
usage in half for the dominant value type allows double the waves to be
resident simultaneously on AMD GCN3+/RDNA and NVIDIA Volta+. For a shader
sitting at the boundary between 4-wave and 8-wave occupancy, the switch to
FP16 can double occupancy and, in a latency-limited regime, double throughput.
This is specifically valuable in pixel shaders that accumulate lighting
contributions across many samples, where each `float4` held alive across a
loop iteration costs 4 VGPRs.

The precision trade-off must be verified: `min16float` has approximately 3.3
decimal digits of precision (10-bit mantissa), versus 7.2 for `float`. For
colour mathematics operating in [0, 1] and written to an 8-bit target (which
itself has only 2.4 digits of precision), the FP16 rounding error is dominated
by the target quantisation and is indistinguishable in the output. For depth
values, world-space positions, or any value with a wide dynamic range, the
suggestion must be reviewed by a human before applying.

## Examples

### Bad

```hlsl
// Colour accumulation in float — ALU-bound inner loop that could use min16float.
float4 ps_lighting(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 albedo = AlbedoTex.Sample(SS, uv);
    float4 light  = LightTex.Sample(SS, uv);
    float  ndotl  = saturate(dot(Normal, LightDir));
    return saturate(albedo * light * ndotl);  // output to 8-bit RT
}
```

### Good

```hlsl
// min16float4 in [0,1]; packed FP16 VALU on RDNA; half the VGPR pressure.
float4 ps_lighting(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    min16float4 albedo = (min16float4)AlbedoTex.Sample(SS, uv);
    min16float4 light  = (min16float4)LightTex.Sample(SS, uv);
    min16float  ndotl  = (min16float)saturate(dot(Normal, LightDir));
    return (float4)saturate(albedo * light * ndotl);
}
```

## Options

none — The rule fires as an `info`-level suggestion; it never triggers
automatically. Suppression:

```hlsl
// shader-clippy: allow(min16float-opportunity)
float4 albedo = AlbedoTex.Sample(SS, uv);
```

## Fix availability

**suggestion** — A candidate rewrite is shown, but the precision trade-off must
be verified by the author. The rule offers the suggested change with a
human-confirmation prompt when run with `--fix`.

## See also

- Related rule: [min16float-in-cbuffer-roundtrip](min16float-in-cbuffer-roundtrip.md) —
  unnecessary 32-to-16 demotion when reading a cbuffer float as min16float
- Related rule: [manual-f32tof16](manual-f32tof16.md) — hand-rolled bit-twiddling
  for FP32-to-FP16 conversion
- HLSL intrinsic reference: `min16float`, `min10float`, `half` — minimum
  precision types in HLSL
- Companion blog post: [packed-math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/min16float-opportunity.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
