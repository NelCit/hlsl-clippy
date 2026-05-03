---
id: waveops-include-helper-lanes-on-non-pixel
category: wave-helper-lane
severity: error
applicability: machine-applicable
since-version: v0.3.0
phase: 3
language_applicability: ["hlsl"]
---

# waveops-include-helper-lanes-on-non-pixel

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

The `[WaveOpsIncludeHelperLanes]` attribute applied to an entry-point function whose stage is not pixel. The SM 6.7 attribute is meaningful only on pixel shaders, where the wave includes helper lanes that exist solely to compute derivatives for adjacent quad lanes. On every other stage there are no helper lanes, so the attribute is at best ignored and at worst a hard validator error. Slang reflection identifies the stage; the rule fires when the attribute appears anywhere outside a `[shader("pixel")]` entry.

## Why it matters on a GPU

Helper lanes are a pixel-shader-specific concept. On NVIDIA Turing/Ada Lovelace, when a quad of pixels is partially covered, the rasterizer launches the wave with the uncovered lanes marked as helpers — they execute the same shader code so derivatives (`ddx` / `ddy`) can be computed by quad-message-passing, but their stores are masked off. AMD RDNA 2/3 implements the same model. Intel Xe-HPG is identical.

By default, wave intrinsics (`WaveActiveSum`, `WavePrefixSum`, `WaveActiveBallot`, etc.) participate only on lanes that are *not* helpers — the helper lanes are excluded from the active mask. SM 6.7 added `[WaveOpsIncludeHelperLanes]` so authors can opt back in to including helpers when the wave-op semantics tolerate it (typically when the helper-lane values are well-defined). Outside pixel, there are no helpers; the attribute is meaningless.

DXC issues a warning today for the off-stage use; the SM 6.7 spec promotes the case to a hard rule so the validator can catch it definitively. Catching it at lint time also catches the cases where the entry stage is set indirectly (a function that's a `[shader("pixel")]` entry in one PSO and a `[shader("compute")]` entry in another).

## Examples

### Bad

```hlsl
// Compute shader cannot have helper lanes; attribute is meaningless / error.
[WaveOpsIncludeHelperLanes]
[numthreads(64, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    uint sum = WaveActiveSum(tid);
    /* ... */
}
```

### Good

```hlsl
// Either drop the attribute (compute) or move to a PS entry.
[numthreads(64, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    uint sum = WaveActiveSum(tid);
    /* ... */
}
```

## Options

none

## Fix availability

**machine-applicable** — Removing the attribute is a textual deletion. `shader-clippy fix` deletes the attribute from non-pixel entries.

## See also

- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — companion helper-lane rule
- Related rule: [quadany-quadall-non-quad-stage](quadany-quadall-non-quad-stage.md) — sibling quad-stage validation
- HLSL specification: [SM 6.7 WaveOpsIncludeHelperLanes attribute](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_7.html)
- Companion blog post: [wave-helper-lane overview](../blog/wave-helper-lane-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/waveops-include-helper-lanes-on-non-pixel.md)

*© 2026 NelCit, CC-BY-4.0.*
