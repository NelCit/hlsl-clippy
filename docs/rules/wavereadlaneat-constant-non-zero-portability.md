---
id: wavereadlaneat-constant-non-zero-portability
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# wavereadlaneat-constant-non-zero-portability

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `WaveReadLaneAt(x, K)` call where `K` is a compile-time non-zero constant in an entry point that lacks a `[WaveSize(N)]` (or `[WaveSize(min, max)]`) attribute pinning the wave size. The detector folds the second argument to a constant if possible, reads the entry's `[WaveSize]` attribute via reflection, and fires when the constant is `>= 32` (potentially out-of-range on a 32-wide wave) or, more tightly, when the constant is `>= min(supported_wave_sizes_on_target)`. The companion AST-only rule `wavereadlaneat-constant-zero-to-readfirst` (Phase 2) handles the `K == 0` case; this rule handles `K != 0`.

## Why it matters on a GPU

`WaveReadLaneAt(x, K)` returns the value of `x` from lane `K` within the current wave. The valid range of `K` is `[0, WaveGetLaneCount() - 1]`. On AMD RDNA 1/2/3 the hardware supports both 32-wide and 64-wide waves; the driver picks one per-PSO based on hints and register pressure. On NVIDIA Turing and Ada Lovelace the wave is always 32 lanes. On Intel Xe-HPG the SIMD width is 8, 16, or 32 lanes depending on the compiler's choice. A `WaveReadLaneAt(x, 47)` is valid on RDNA wave64, undefined on RDNA wave32 / Ada (lane index out of range), and undefined on Xe-HPG wave16 / wave32. The HLSL spec says out-of-range lane indices produce undefined results — in practice the hardware returns garbage or zero depending on the IHV.

Without `[WaveSize]`, the developer cannot rely on the wave size at any specific call site, but a constant `K = 47` baked into the source unmistakably implies a wave64 assumption. The bug is that the assumption is invisible to readers — the constant looks innocuous — and the failure mode is silent: the shader runs to completion on wave32 and produces wrong values, with no compiler warning and no runtime fault.

The fix is one of: pin the wave size with `[WaveSize(64)]` if the algorithm requires wave64, refactor the call to a wave-size-agnostic alternative (`WaveActive*` reductions don't carry a lane index), or constrain the constant to `K < 32` so it works on every supported wave size. The rule surfaces the choice; the author owns the resolution.

## Examples

### Bad

```hlsl
// No [WaveSize]; constant lane index implies wave64 but isn't guaranteed.
[numthreads(64, 1, 1)]
void cs_unpinned(uint gi : SV_GroupIndex) {
    float v = GroupshareInput[gi];
    // K = 47 is OOB on wave32 (Ada, RDNA wave32 mode, Xe-HPG wave32).
    float broadcast = WaveReadLaneAt(v, 47);
    Output[gi] = broadcast;
}
```

### Good

```hlsl
// Pin the wave size to match the assumed lane index range.
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_pinned64(uint gi : SV_GroupIndex) {
    float v = GroupshareInput[gi];
    float broadcast = WaveReadLaneAt(v, 47);  // valid on wave64
    Output[gi] = broadcast;
}

// Or constrain K to a portable range and let any wave size run.
[numthreads(64, 1, 1)]
void cs_portable(uint gi : SV_GroupIndex) {
    float v = GroupshareInput[gi];
    float broadcast = WaveReadLaneAt(v, 7);  // valid on every supported size
    Output[gi] = broadcast;
}
```

## Options

none

## Fix availability

**suggestion** — Pinning wave size or refactoring the lane index has structural consequences. The diagnostic identifies the constant-K vs wave-size mismatch; the author chooses the resolution.

## See also

- Related rule: [wavesize-attribute-missing](wavesize-attribute-missing.md) — broader wave-size-dependence detector
- Related rule: [wavereadlaneat-constant-zero-to-readfirst](wavereadlaneat-constant-zero-to-readfirst.md) — Phase 2 AST-only sibling for `K == 0`
- HLSL intrinsic reference: `WaveReadLaneAt`, `WaveGetLaneCount`, `[WaveSize]` in the DirectX HLSL Shader Model 6.0+ documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wavereadlaneat-constant-non-zero-portability.md)

*© 2026 NelCit, CC-BY-4.0.*
