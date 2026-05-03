---
id: compute-dispatch-grid-shape-vs-quad
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# compute-dispatch-grid-shape-vs-quad

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A compute or amplification entry point declared with `[numthreads(N, 1, 1)]` (a 1D thread-group shape) whose body invokes `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, `ddx_coarse`, `ddy_coarse`, `QuadReadAcrossX`, `QuadReadAcrossY`, `QuadReadAcrossDiagonal`, or any compute-quad derivative-bearing intrinsic introduced in SM 6.6. The detector pulls the `[numthreads]` attribute via reflection / AST and scans the entry's reachable AST for the derivative intrinsics. It does not fire when the thread-group shape is 2D or 3D with both X and Y at least 2 (a real quad layout); it does not fire on pixel shaders (where derivatives are well-defined regardless of dispatch shape).

## Why it matters on a GPU

Shader Model 6.6 added compute-quad derivatives: in a compute shader, `ddx(v)` returns the lane-pair difference within a 2x2 quad of threads, just as in a pixel shader. The mechanism on every IHV requires the hardware to identify a 2x2 quad of lanes whose `SV_GroupThreadID` form an `(x, y)` adjacency. AMD RDNA 2/3 forms quads from lane indices `{0,1,2,3}`, `{4,5,6,7}`, ... within a wave; NVIDIA Turing/Ada use the same lane-pair adjacency for warp-level derivatives; Intel Xe-HPG forms quads from per-channel adjacency. In a 2D `[numthreads(8, 8, 1)]` group, the lane-to-quad mapping naturally pairs `SV_GroupThreadID.x` even/odd lanes and `SV_GroupThreadID.y` even/odd lanes, producing meaningful X and Y derivatives.

In a 1D `[numthreads(64, 1, 1)]` group, every lane sits at `SV_GroupThreadID.y == 0`. The hardware quad selector still identifies four-lane neighborhoods, but the resulting `ddx` values are the difference between lanes laid out linearly (e.g. lanes 0/1 vs lanes 2/3 within each quad) and `ddy` values are zero — the Y dimension is degenerate. Whatever the consumer expects from the derivative (texture LOD selection, screen-space tangent, finite-difference normals) operates on garbage. The compiler does not warn: the derivative intrinsics are valid in any compute shader, the hardware happily computes them, and the result is simply meaningless for the algorithm.

The fix is to reshape the thread-group to a 2D layout (e.g. `[numthreads(8, 8, 1)]` for the same total of 64 threads) and reinterpret `SV_DispatchThreadID` accordingly. This is a structural change — index arithmetic in the body needs updating — so the rule is a suggestion that surfaces the dispatch-shape mismatch and lets the author own the refactor.

## Examples

### Bad

```hlsl
[numthreads(64, 1, 1)]
void cs_derivs_in_1d(uint3 dtid : SV_DispatchThreadID) {
    float2 uv = float2(dtid.x, 0) / 64.0f;
    // ddy is structurally zero in a 1D group; ddx is lane-stride, not screen.
    float2 derivs = float2(ddx(uv.x), ddy(uv.y));
    Output[dtid.xy] = derivs;  // y component is always 0
}
```

### Good

```hlsl
[numthreads(8, 8, 1)]
void cs_derivs_in_2d(uint3 dtid : SV_DispatchThreadID) {
    float2 uv = float2(dtid.xy) / 64.0f;
    // 2x2 quad in (x, y) — both ddx and ddy carry real derivative info.
    float2 derivs = float2(ddx(uv.x), ddy(uv.y));
    Output[dtid.xy] = derivs;
}
```

## Options

none

## Fix availability

**suggestion** — The fix changes the dispatch shape (and the application's `Dispatch(X, Y, Z)` call). The diagnostic identifies the 1D-with-derivatives mismatch and the candidate 2D shape; the author owns the index-arithmetic refactor.

## See also

- Related rule: [numthreads-not-wave-aligned](numthreads-not-wave-aligned.md) — thread-group total vs wave size
- Related rule: [wavesize-attribute-missing](wavesize-attribute-missing.md) — wave-size dependence in the same family
- HLSL reference: compute-quad derivatives in the DirectX HLSL Shader Model 6.6 documentation
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/compute-dispatch-grid-shape-vs-quad.md)

*© 2026 NelCit, CC-BY-4.0.*
