---
title: "pow(x, 2.0) is hiding a transcendental in your shader"
date: 2026-04-30
author: NelCit
rule-id: pow-const-squared
tags: [hlsl, shaders, performance, math, transcendentals]
license: CC-BY-4.0
---

> Companion post for the [pow-const-squared](../../rules/pow-const-squared) rule.
> Shipped in v0.5.6 (Phase 0); see [CHANGELOG](../../../CHANGELOG.md) for the release history.

You wrote `pow(x, 2.0)` because it reads cleaner than `x * x`, the intent is
self-documenting, and surely the compiler handles this. It often does — and
when it doesn't, you've quietly promoted a full-rate multiply into a
quarter-rate transcendental sequence that runs on a completely different
execution unit. This post explains exactly what that means on modern GPU
hardware and when you can trust the compiler to bail you out.

## What pow(x, n) actually lowers to

HLSL's `pow(x, n)` is defined as `x^n` for `x >= 0`. The canonical lowering
— used by both DXC and FXC on D3D11/D3D12 targets — is:

```
pow(x, n)  =>  exp2(n * log2(x))
```

This is mathematically equivalent by the change-of-base identity
`x^n = 2^(n * log2(x))`, and it maps cleanly to the two transcendental
instructions every GPU exposes: `log2` and `exp2`. There is no "raise to a
power" opcode in HLSL bytecode or DXIL; the compiler always decomposes the
call.

For `pow(x, 2.0)` specifically, `n` is constant, so the multiply collapses:

```
pow(x, 2.0)  =>  exp2(2.0 * log2(x))
               =>  exp2(log2(x) + log2(x))   -- or just 2 * log2(x)
```

You get: one `log2`, one multiply-by-two (or an add), one `exp2`. Three
instructions. The multiply-by-two is effectively free in the scalar case, but
`log2` and `exp2` are not.

## The SFU: why transcendentals are slow

Modern GPUs execute floating-point arithmetic on the **Vector ALU (VALU)**
— the wide SIMD engine that processes all lanes of a wave in lockstep. The
VALU is optimized for throughput on addition, multiplication, and
multiply-add. These are full-rate: on RDNA2 in wave32 mode, `v_mul_f32`
issues at one instruction per clock per SIMD unit.

Transcendental functions — `log2`, `exp2`, `sin`, `cos`, `sqrt`, `rcp` —
are handled by a separate unit: the **Special Function Unit (SFU)**, called
the **Transcendental Execution Unit** in AMD documentation and the **SFU**
in NVIDIA terminology. The SFU has narrower issue width than the VALU.

On AMD RDNA2 (the architecture behind RX 6000-series, PS5, Xbox Series S/X):

- VALU full-rate ops (`v_mul_f32`, `v_add_f32`, `v_fma_f32`): **1 instruction
  per clock** per SIMD32 unit.
- VALU quarter-rate ops (`v_log2_f32`, `v_exp2_f32`, `v_sin_f32`): **1
  instruction per 4 clocks** per SIMD32 unit (the SFU within each CU
  processes one quarter of the wave lanes per cycle).

On NVIDIA Ampere (RTX 3000-series) and Turing (RTX 2000-series), the SFU
is similarly narrower than the full FP32 pipeline. NVIDIA's Turing whitepaper
describes the SM as having 128 FP32 CUDA cores and 32 SFU units — a 4:1
ratio, implying quarter-rate throughput for transcendentals at the warp level.

The practical consequence: `v_log2_f32` takes roughly 4 clocks to complete
for a full RDNA2 wave, and `v_exp2_f32` takes another 4. A multiply is 1.

## Concrete instruction counts

For `pow(x, 2.0)` with no compiler folding:

```
v_log2_f32   vX, vX         ; ~4 clocks (SFU)
v_add_f32    vX, vX, vX     ; ~1 clock  (VALU)  -- or v_mul_f32 with literal 2.0
v_exp2_f32   vX, vX         ; ~4 clocks (SFU)
```

Total: approximately 9 clocks of SIMD latency per wave (ignoring instruction
fetch, issue slot conflicts, and memory ops that could hide some of this).

For `x * x`:

```
v_mul_f32    vX, vX, vX     ; ~1 clock  (VALU)
```

Total: approximately 1 clock.

**The ratio is roughly 9:1 for this instruction sequence in isolation.**

A note on how to interpret this: GPU throughput is about instruction issue
rate, not just latency, and out-of-order execution can hide latency when there
is independent work to schedule. In a well-pipelined shader with lots of
independent arithmetic, the SFU's 4-cycle throughput penalty can be partially
hidden. In a shader dominated by dependent operations — or in a tight pixel
shader where latency is exposed — you pay the full cost. The 9:1 figure is
the worst case; real-world gains vary, but the direction is always the same.

## Why the compiler doesn't always save you

Both DXC and FXC are aware of the `pow(x, 2.0) -> x * x` substitution, and
they apply it opportunistically. But "opportunistically" has real limits.

**When the fold usually happens:**

- `x` is a simple scalar identifier — a local float temp with a single
  definition. The optimizer can trivially duplicate the use.
- The exponent is the integer literal `2` or the float literal `2.0`.
- There is no `precise` qualifier on the function or the expression. `precise`
  disables reassociation and strength reduction to protect floating-point
  reproducibility, so the optimizer backs off.

**When the fold often does not happen:**

- `x` is a complex subexpression: `pow(dot(N, L) * attenuation + ambient, 2.0)`.
  The optimizer may decide the cost of computing the subexpression twice
  outweighs the transcendental saving, or it may simply not track value
  reuse across the CSE boundary correctly.
- The shader model target is SM4.x / D3D11 HLSL and the optimizer is FXC.
  FXC's strength-reduction pass is older and less aggressive than DXC's.
- The exponent is `2.0f` typed as a half (`min16float`) or there is a type
  mismatch between the base and the exponent that triggers a cast path.
- The call is inside a function with `precise` return semantics — this is
  more common than it looks; many engine codebases tag utility math as
  `precise` for determinism across platforms.

In practice, if you are compiling with DXC targeting SM6.x and `x` is a
simple float local, you will usually get the fold. If you are targeting an
older SM, using FXC, or `x` has any complexity, the fold is not guaranteed.
The `pow-const-squared` rule catches the cases the compiler misses — and
on codebases that mix SM targets or both DXC and FXC (engine editors often
do), it provides a consistent audit baseline.

## The Schlick angle: where this costs you for real

The most common place this pattern appears in production shaders is the
Schlick approximation for Fresnel reflectance. The standard formulation is:

```hlsl
float SchlickFresnel(float F0, float NdotV)
{
    return F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
}
```

`pow(1.0 - NdotV, 5.0)` is `(1 - NdotV)^5`. With constant exponent 5, DXC
typically does not fold this to multiplications — 5 is not in the small
power-of-two fast path that most compilers check first. Expanded by hand:

```hlsl
float SchlickFresnel(float F0, float NdotV)
{
    float x  = 1.0 - NdotV;
    float x2 = x * x;
    float x4 = x2 * x2;
    return F0 + (1.0 - F0) * (x4 * x);   // x^5 = x^4 * x^1
}
```

This version is 4 full-rate multiplies and 2 subtracts — all VALU, no SFU.
The original is `v_log2_f32 + v_mul_f32 + v_exp2_f32` — 8+ clocks of SFU
overhead.

In a PBR G-buffer pixel shader covering a large surface at screen-filling
resolution (say 1920x1080), every pixel in the covered area executes this
function. At 60 fps, that is 60 × 1920 × 1080 = ~124 million invocations per
second. The SFU overhead is a direct contribution to shader occupancy limits
and fill-rate budget. Small shaders are dominated by memory bandwidth; complex
material shaders accumulate enough arithmetic that transcendental throughput
becomes a real limit.

The `pow-const-squared` rule as initially defined catches exponents 2, 3, and
4. For exponent 5 (Schlick), the break-even is higher and is tracked
separately. But the mechanism — and the fix — are identical.

## Using shader-clippy to catch this

Once the `pow-const-squared` rule ships in v0.1, running:

```
shader-clippy lint shaders/
```

will emit a warning on every `pow(x, n)` call where `n` is a constant integer
in `{2, 3, 4}`, with a suggested replacement:

```
warning[pow-const-squared]: pow(roughness, 2.0) uses two quarter-rate SFU ops;
  replace with roughness * roughness
  --> Material.hlsl:47:20
```

If you have verified that your specific compiler + SM target folds the pattern
correctly, you can suppress per-callsite:

```hlsl
float r = pow(roughness, 2.0); // shader-clippy: allow(pow-const-squared)
```

The full rule specification — including edge cases and the exact exponent
range — lives in the [pow-const-squared rule doc](../../rules/pow-const-squared).

## A note on profiling

This post describes a throughput penalty that is real and measurable. Whether
it matters in your shader depends on whether the SFU is your bottleneck.
A shader that is bandwidth-bound or waiting on texture fetch will not improve
from removing `pow` calls. The right workflow is:

1. Profile first. Identify bound resource (ALU, SFU, L1/L2, ROB, memory).
2. If SFU is in the hot path, `pow-const-squared` is actionable.
3. If bandwidth is the bottleneck, look elsewhere.

`shader-clippy` is a static linter, not a profiler. It flags portable patterns
that are provably suboptimal at the ISA level. The profiler tells you which
patterns are actually costing you frames. Use both.

---

`shader-clippy` is an open-source HLSL linter. Rules, issues, and discussion
live at [github.com/NelCit/shader-clippy](https://github.com/NelCit/shader-clippy).
If you have encountered a shader pattern that should be a lint rule, open an
issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
