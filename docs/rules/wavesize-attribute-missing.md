---
id: wavesize-attribute-missing
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# wavesize-attribute-missing

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

A compute or amplification entry point that uses wave intrinsics in a way whose result depends on the runtime wave size — e.g. `WaveGetLaneCount()` consumed by an arithmetic expression, `WaveReadLaneAt(x, K)` with `K >= 32`, fixed-stride reductions of the form `lane + 32`, or groupshared layouts indexed by `WaveGetLaneCount()` — without a corresponding `[WaveSize(N)]` or `[WaveSize(min, max)]` attribute on the entry. The detector reads the entry's `[WaveSize]` attribute via reflection and scans the AST for wave-size-dependent uses. It does not fire when `[WaveSize]` is present, nor when the only wave intrinsics in use are wave-size-agnostic (`WaveActiveSum`, `WavePrefixSum`, `WaveActiveBitOr`, `WaveActiveAllTrue`).

## Why it matters on a GPU

Hardware wave size varies across IHVs and even across architectures from the same IHV. AMD RDNA 1/2/3 supports both 32-wide and 64-wide waves; the driver picks one based on the shader's hints (`[WaveSize]`, register pressure, SM version). NVIDIA Turing and Ada Lovelace are always 32-wide warps. Intel Xe-HPG SIMD width is 8, 16, or 32 lanes depending on register pressure and compiler decisions. A shader that relies on a specific wave size — e.g. assumes a wave covers exactly 32 lanes — runs correctly on Turing/Ada but produces silently wrong results on RDNA when the driver picks 64-wide, or on Xe-HPG when the compiler picks 16-wide.

`[WaveSize(N)]` (SM 6.6) and `[WaveSize(min, max, preferred)]` (SM 6.8) tell the driver the shader requires a specific wave size; the driver honours the request when the hardware supports it and rejects PSO creation when it cannot. With the attribute, the wave-size-dependent code is portable: the driver guarantees the assumption holds. Without it, RDNA may run the shader at wave64 with silently doubled lane counts, Xe-HPG may run it at wave16 with silently halved lane counts, and any code that hard-codes "32" against `WaveGetLaneCount()` produces wrong indices, wrong reduction sums, or wrong groupshared layouts.

The rule pulls the wave-size dependence to lint time. The fix is to add `[WaveSize(32)]` (or whichever pinned size the algorithm assumes); the alternative is to refactor the shader to use only wave-size-agnostic intrinsics, which the rule does not auto-suggest because the refactor depth varies.

## Examples

### Bad

```hlsl
// No [WaveSize]; shader hard-codes 32-lane assumptions.
[numthreads(64, 1, 1)]
void cs_lane32_assumed(uint gi : SV_GroupIndex) {
    // WaveReadLaneAt(x, 32) is OOB on wave32 (RDNA wave32 mode, Ada),
    // valid on wave64 (RDNA wave64 mode).
    float v = WaveReadLaneAt(GroupshareInput[gi], 32);
    Output[gi] = v;
}
```

### Good

```hlsl
// Pin wave size; driver guarantees the lane-count assumption holds.
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_lane32_pinned(uint gi : SV_GroupIndex) {
    float v = WaveReadLaneAt(GroupshareInput[gi], 31);  // valid in wave32
    Output[gi] = v;
}

// Or refactor to wave-size-agnostic intrinsics:
[numthreads(64, 1, 1)]
void cs_agnostic(uint gi : SV_GroupIndex) {
    float v = WaveActiveSum(GroupshareInput[gi]);  // works at any wave size
    Output[gi] = v;
}
```

## Options

none

## Fix availability

**suggestion** — Adding `[WaveSize(N)]` may cause PSO creation to fail on hardware that cannot honour the request (e.g. older RDNA 1 in 32-wide mode lacks the `[WaveSize]` attribute support). The diagnostic flags the wave-size dependence; the author chooses pinning vs refactoring.

## See also

- Related rule: [wavereadlaneat-constant-non-zero-portability](wavereadlaneat-constant-non-zero-portability.md) — specific case of constant-K WaveReadLaneAt
- Related rule: [numthreads-not-wave-aligned](numthreads-not-wave-aligned.md) — thread-group total vs wave size
- HLSL reference: `[WaveSize]` attribute, `WaveGetLaneCount`, wave intrinsics in the DirectX HLSL Shader Model 6.6+ documentation
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wavesize-attribute-missing.md)

*© 2026 NelCit, CC-BY-4.0.*
