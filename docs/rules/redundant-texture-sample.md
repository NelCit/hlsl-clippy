---
id: redundant-texture-sample
category: memory
severity: warn
applicability: none
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# redundant-texture-sample

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Two or more calls to the same texture's `Sample`, `SampleLevel`, or
`SampleGrad` method with identical arguments — the same texture object, the
same sampler, and the same UV coordinates — appearing in the same basic block
with no intervening write to the texture or to the UV value. The compiler's
built-in common-subexpression elimination (CSE) pass normally handles this,
but it may fail when the duplicate calls are separated by a function call
boundary, when the texture object is passed as a parameter (obscuring aliasing
information), or when the compiler's alias analysis conservatively assumes the
function may write the texture. The rule fires on the second (and subsequent)
sample of any (texture, sampler, UV) triple that has already been sampled in
the same basic block.

## Why it matters on a GPU

A texture sample is not free: the hardware must compute the MIP-map level,
look up the texel footprint, fetch the relevant cache lines from L1 (the
texture L0 cache on RDNA, or the TEX cache on Turing), interpolate the
samples, and return a result to the ALU pipeline. On RDNA 3, a cache-hit
sample from L0 still costs roughly 22 cycles of latency; an L1 miss escalates
to ~100 cycles and an L2 miss to ~200-300 cycles. A redundant sample that
could have been eliminated at compile time pays this latency again
unnecessarily, and occupies a TMU slot that could have served a different
wave's pending request.

The L1 texture cache is typically small: 32 KB per shader engine on RDNA,
shared across multiple CUs. A redundant sample for the same texel as a
previous one in the same wave is not guaranteed to hit L0, because the result
of the first sample may have been consumed and the cache line replaced between
the two sample instructions if other samples intervened. Even when the result
does hit L0, the second sample still issues as an independent instruction,
consuming a TMU dispatch slot and requiring the result path to be wired back
to the register file. Eliminating the second sample by re-using the SSA value
from the first is strictly better in every case.

Compiler CSE is reliable within a single basic block when all expressions are
in SSA form, but HLSL's function call model can fool the alias analysis into
treating a `SampleLevel` call as a potential texture-write if the texture
object or sampler is passed by reference. The IR-level rule catches the cases
that survive into the final DXIL instruction stream.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/register_pressure.hlsl — HIT(redundant-texture-sample)
float4 ps_duplicate_sample(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 first  = TexA.Sample(SS, uv);     // sample #1
    float4 second = TexA.Sample(SS, uv);     // HIT: identical tex+UV, same basic block
    return lerp(first, second, Blend) * Exposure;
}
```

### Good

```hlsl
// Cache the result in a local variable and re-use it.
float4 ps_cached_sample(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 texel = TexA.Sample(SS, uv);
    return lerp(texel, texel, Blend) * Exposure;
    // (In this contrived case lerp(x, x, t) == x, but the point is re-use.)
}

// Two different UVs — not redundant.
float4 ps_two_different_samples(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 s0 = TexA.Sample(SS, UV0);
    float4 s1 = TexA.Sample(SS, UV1);
    return lerp(s0, s1, Blend) * Exposure;
}
```

## Options

none

## Fix availability

**none** — Although the fix is conceptually simple (cache the first result in
a local variable), the IR-level rule fires after the compiler has already
compiled the shader. Automatically patching the HLSL source would require
matching the IR pattern back to the original HLSL expression, which is fragile
across inlining and optimisation transforms. The diagnostic identifies the
source line of the second sample; the fix is a one-line change.

## See also

- Related rule: [vgpr-pressure-warning](vgpr-pressure-warning.md) — caching a
  sample result adds one VGPR live range but removes the second TMU issue
- HLSL intrinsic reference: `Sample`, `SampleLevel`, `SampleGrad` in the
  DirectX HLSL Intrinsics documentation
- Companion blog post: [memory overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/redundant-texture-sample.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
