---
id: wavesize-range-disordered
category: wave-helper-lane
severity: error
applicability: machine-applicable
since-version: v0.2.0
phase: 2
language_applicability: ["hlsl"]
---

# wavesize-range-disordered

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `[WaveSize(min, preferred, max)]` or `[WaveSize(min, max)]` attribute whose constant arguments are not in non-decreasing order. The SM 6.8 attribute requires `min <= preferred <= max`; the SM 6.6 two-arg form requires `min <= max`. Constant-fold the integer arguments and fire when the ordering is violated. Common authoring slip: a copy-paste of `[WaveSize(64, 32)]` from a wave64-then-wave32 RDNA snippet.

## Why it matters on a GPU

`[WaveSize]` is the contract between the shader author and the compiler about which wave widths the shader is willing to run on. The compiler uses the bounds to pick an instruction-selection strategy (RDNA 1/2/3 can run wave32 or wave64 depending on the kernel; NVIDIA Turing/Ada is fixed at wave32; Intel Xe-HPG runs wave8/16/32 depending on register pressure). When the bounds are well-ordered, the runtime picks an in-range wave size at dispatch time and the shader runs as designed.

When the bounds are *not* well-ordered, the behaviour depends on the toolchain: DXC produces an error, the Slang compiler produces an error, and the D3D12 runtime rejects the PSO. None of these failure modes is helpful to the author — they all point at the attribute without explaining the swap. Catching it at lint time turns the failure into a source-located diagnostic with a precise fix: swap the constants.

The `preferred` middle argument (SM 6.8 three-arg form) adds a hint to the runtime about which wave size to prefer when the dispatch is small enough that the choice is free. Authors often confuse the slot order between the two-arg and three-arg forms; the lint catches both.

## Examples

### Bad

```hlsl
// min=64, max=32 — disordered; PSO creation fails.
[WaveSize(64, 32)]
[numthreads(64, 1, 1)]
void Compute(uint tid : SV_DispatchThreadID) { /* ... */ }

// SM 6.8: min=32, preferred=64, max=32 — also disordered.
[WaveSize(32, 64, 32)]
[numthreads(64, 1, 1)]
void ComputeSm68(uint tid : SV_DispatchThreadID) { /* ... */ }
```

### Good

```hlsl
// Non-decreasing ordering.
[WaveSize(32, 64)]
[numthreads(64, 1, 1)]
void Compute(uint tid : SV_DispatchThreadID) { /* ... */ }

[WaveSize(32, 64, 64)]
[numthreads(64, 1, 1)]
void ComputeSm68(uint tid : SV_DispatchThreadID) { /* ... */ }
```

## Options

none

## Fix availability

**machine-applicable** — When the disorder is a swap of two arguments and the swap produces a well-ordered attribute, `hlsl-clippy fix` rewrites the attribute. When the bounds need a deeper fix (e.g., a semantic change to the kernel's wave-size assumption), the rule emits a suggestion-tier diagnostic instead.

## See also

- Related rule: [wavesize-attribute-missing](wavesize-attribute-missing.md) — wave intrinsics without `[WaveSize]`
- Related rule: [wavesize-fixed-on-sm68-target](wavesize-fixed-on-sm68-target.md) — fixed `[WaveSize(N)]` on SM 6.8 target
- HLSL specification: [SM 6.8 WaveSize range form](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html)
- Companion blog post: [wave-helper-lane overview](../blog/wave-helper-lane-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wavesize-range-disordered.md)

*© 2026 NelCit, CC-BY-4.0.*
