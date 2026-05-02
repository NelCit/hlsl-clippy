---
id: vgpr-pressure-warning
category: memory
severity: warn
applicability: none
since-version: v0.7.0
phase: 7
---

# vgpr-pressure-warning

> **Pre-v0 status:** this rule is documented ahead of implementation. The
> detection logic requires IR-level analysis of compiled DXIL and is not yet
> wired into the linter pipeline.

## What it detects

A live-range-based static estimate of per-lane VGPR consumption for each
entry-point function in the compiled DXIL. The rule fires when the estimated
peak live register count exceeds a configurable per-stage threshold. The
estimate counts simultaneously live scalar and vector values after register
allocation; it correlates with, but does not exactly reproduce, what the
compiler back-end reports. The rule fires at the function boundary and
identifies the source region (line range in the original HLSL) that drives the
peak.

## Why it matters on a GPU

Every VGPR (vector general-purpose register) allocated to a shader is
multiplied across every lane in a wave. On AMD RDNA 2/3, a wave is 32 or 64
lanes wide, and the hardware VGPR file holds 1536 registers per compute unit
per SIMD32 unit. If a shader requires 80 VGPRs per lane, only 1536/80 = 19
waves can be resident concurrently per SIMD32 — barely two full waves. At 64
VGPRs the number rises to 24 waves; at 32 VGPRs it reaches the hardware
maximum of 48 waves. Fewer resident waves means the scheduler has fewer
instruction streams to overlap with memory latency, and the arithmetic units
stall waiting for texture or buffer returns. The effect is especially
pronounced in pixel shaders with many simultaneous texture fetches, where
latency-hiding determines whether the TMU operates at peak throughput.

On NVIDIA Turing and Ada Lovelace, each SM has 65536 32-bit registers shared
among resident warps. A shader using 96 registers per thread supports at most
65536/96 = 682 threads resident per SM, well below the theoretical maximum
of 1024. Every additional VGPR consumed by a long live range costs occupancy,
and occupancy is the primary lever for hiding the 100-300 cycle latency of
L2-miss texture samples. The practical impact is a step function: shaders
that cross common VGPR thresholds (32, 64, 80, 96, 128) drop to a lower
occupancy tier abruptly.

Reducing peak live ranges is the correct fix: restructure accumulations into
sequential patterns (accumulate into one variable rather than holding all
intermediate values live simultaneously), break long chains with temporaries
that reuse registers, or split a high-VGPR entry point into multiple smaller
dispatch passes. The `ps_low_pressure` pattern in the fixture — sequential
`acc +=` rather than holding 12 live `float4` values concurrently — is the
canonical fix.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/register_pressure.hlsl — HIT(vgpr-pressure-warning)
// 12 live float4 values concurrently — 192 bytes of VGPR per lane.
float4 ps_high_pressure(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 a0 = TexA.Sample(SS, uv + float2(0.00, 0.00));
    float4 a1 = TexA.Sample(SS, uv + float2(0.01, 0.00));
    float4 a2 = TexA.Sample(SS, uv + float2(0.02, 0.00));
    float4 a3 = TexA.Sample(SS, uv + float2(0.03, 0.00));
    float4 b0 = TexB.Sample(SS, uv + float2(0.00, 0.01));
    // ... 7 more live float4s ...
    return (a0+a1+a2+a3 + b0+...) * (Exposure / 12.0);
}
```

### Good

```hlsl
// Sequential accumulation — at most 2 live float4 values at once.
float4 ps_low_pressure(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 acc = 0;
    acc += TexA.SampleLevel(SS, uv + float2(0.00, 0.00), 0);
    acc += TexA.SampleLevel(SS, uv + float2(0.01, 0.00), 0);
    acc += TexB.SampleLevel(SS, uv + float2(0.00, 0.01), 0);
    acc += TexC.SampleLevel(SS, uv + float2(0.00, 0.02), 0);
    return acc * (Exposure / 4.0);
}
```

## Options

- `threshold-per-stage` (integer array, default: `[64, 80, 80, 96, 96]`) —
  per-stage VGPR thresholds in the order `[VS, PS, CS, AS, MS]`. Stages not
  in this list use the CS value as the default. Set any entry to `0` to disable
  the check for that stage.

## Fix availability

**none** — The rule can identify the high-pressure source region but cannot
automatically restructure the live ranges. Refactoring requires understanding
the intended computation; the fix is always manual.

## See also

- Related rule: [scratch-from-dynamic-indexing](scratch-from-dynamic-indexing.md) — a
  related cause of register-file pressure
- Related rule: [redundant-texture-sample](redundant-texture-sample.md) —
  eliminating redundant samples reduces peak live count
- Related rule: [live-state-across-traceray](live-state-across-traceray.md) — ray
  stack spill from long live ranges crossing TraceRay
- Companion blog post: [memory overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/vgpr-pressure-warning.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
