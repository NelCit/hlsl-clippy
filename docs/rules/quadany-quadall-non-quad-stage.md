---
id: quadany-quadall-non-quad-stage
category: wave-helper-lane
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# quadany-quadall-non-quad-stage

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A call to `QuadAny(...)`, `QuadAll(...)`, `QuadReadAcrossX`, `QuadReadAcrossY`, or `QuadReadAcrossDiagonal` from an entry stage that does not provide quad semantics. The SM 6.7 quad intrinsics are defined for pixel shaders and for compute shaders launched with a `[numthreads(X, Y, 1)]` shape that produces well-formed 2x2 quads (X and Y both multiples of 2, total threads >= 4). Slang reflection identifies the stage; the rule fires on quad calls from vertex, hull, domain, geometry, mesh, amplification, raygen, closest-hit, any-hit, miss, intersection, and callable shaders, plus from compute shaders whose `[numthreads]` does not satisfy the quad-shape requirement.

## Why it matters on a GPU

Quad intrinsics rely on the hardware launching threads in 2x2 groups so the cross-lane reads (`QuadReadAcross*`) and the per-quad reductions (`QuadAny`/`QuadAll`) have well-defined neighbours. On NVIDIA Turing/Ada Lovelace, the rasterizer guarantees this for pixel shaders by construction (the quad is the rasterizer's primitive), and SM 6.6+ compute supports it when the `[numthreads]` X and Y are even. AMD RDNA 2/3 and Intel Xe-HPG follow the same contract.

In stages without quad semantics — vertex, geometry, raytracing — the hardware has no notion of which lanes are quad-neighbours, so the cross-lane intrinsics either return undefined data or fail validation. The DXC validator catches the simplest mis-uses; the lint catches the rest, including the compute-with-bad-numthreads case where `[numthreads(7, 1, 1)]` or `[numthreads(64, 1, 1)]` (Y == 1) breaks the quad invariant.

The fix is structural: either move the quad intrinsics to a quad-capable stage or refactor the kernel to avoid them. The diagnostic names the offending stage and, for compute, the offending `[numthreads]` shape.

## Examples

### Bad

```hlsl
// Vertex shader has no quad concept.
[shader("vertex")]
VSOut main(VSIn input) {
    bool any = QuadAny(input.uv.x > 0.5);   // ERROR: vertex has no quads
    /* ... */
}

// Compute with [numthreads(64, 1, 1)] — Y == 1, no quads.
[numthreads(64, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    bool any = QuadAny(tid > 16);            // ERROR: numthreads not quad-shaped
}
```

### Good

```hlsl
// Pixel shader: quads are guaranteed by the rasterizer.
[shader("pixel")]
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    bool any = QuadAny(uv.x > 0.5);
    /* ... */
}

// Compute with [numthreads(8, 8, 1)] — quad-shaped.
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    bool any = QuadAny(tid.x > 4);
    /* ... */
}
```

## Options

none

## Fix availability

**none** — Either restructure the entry stage or pick a different `[numthreads]` shape; both are authorial.

## See also

- Related rule: [quadany-replaceable-with-derivative-uniform-branch](quadany-replaceable-with-derivative-uniform-branch.md) — perf rule on the same intrinsic
- Related rule: [quadany-quadall-opportunity](quadany-quadall-opportunity.md) — opposite-direction perf rule
- Related rule: [waveops-include-helper-lanes-on-non-pixel](waveops-include-helper-lanes-on-non-pixel.md) — sibling stage-validation rule
- HLSL specification: [SM 6.7 quad intrinsics](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_7.html)
- Companion blog post: [wave-helper-lane overview](../blog/wave-helper-lane-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/quadany-quadall-non-quad-stage.md)

*© 2026 NelCit, CC-BY-4.0.*
