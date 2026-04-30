---
id: derivative-in-divergent-cf
category: control-flow
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# derivative-in-divergent-cf

> **Status:** pre-v0 — rule scheduled for Phase 4; see [ROADMAP](../../ROADMAP.md).

## What it detects

Calls to `ddx`, `ddy`, `ddx_coarse`, `ddy_coarse`, `ddx_fine`, `ddy_fine`, or texture sample intrinsics that use implicit screen-space derivatives (`Texture.Sample`, `Texture.SampleBias`) when they appear inside a branch whose condition depends on a non-uniform (per-pixel or per-lane varying) value. The rule fires when the condition expression is not provably dynamically uniform — i.e., it is not sourced exclusively from `cbuffer` fields, `nointerpolation` interpolants marked as uniform, or literal constants. Any `if`, `else if`, `else`, or ternary branch that contains such a sample or derivative call and whose predicate includes per-pixel data (interpolated values, SV_Position, texture reads, varying inputs) triggers the diagnostic.

## Why it matters on a GPU

The GPU computes screen-space derivatives by comparing register values across the 2x2 pixel quad that executes together. `ddx(v)` is the difference between the value of `v` in the left column and the right column of the quad; `ddy(v)` is the difference between the top row and the bottom row. For this subtraction to be meaningful, all four pixels in the quad must execute the same instruction at the same program counter simultaneously. When the branch condition is non-uniform, some lanes in the quad take the branch while others do not. The hardware still computes the derivative — by including the lanes that did not execute that instruction — but those lanes produce undefined or stale values. The result is silent data corruption: the mip level chosen by `Sample` will be wrong, gradients fed into `SampleGrad` will be garbage, and any subsequent computation on the derivative is undefined. No GPU exception is raised; the shader simply produces incorrect output.

On AMD RDNA and RDNA 2/3 architectures, derivative computation is performed in the VALU using horizontal operations across the wave. An implicit reconvergence point is not guaranteed at sub-wave granularity, so the four pixels of a quad may not reconverge before the derivative instruction executes. On NVIDIA Turing and Ada Lovelace, quads are always scheduled together within a warp, but divergence still corrupts the subtract because the helper lanes that did not take the branch execute with their last-written register state, not the state they would have had inside the branch. The HLSL specification and the DXIL specification both classify derivative operations inside non-uniform control flow as undefined behaviour; `dxc` does not diagnose it.

The practical consequence is incorrect LOD selection in any pixel shader that discards or branches on a per-pixel condition before sampling a texture. A common production pattern is an alpha-test branch (`if (alpha < threshold) discard;`) followed by `Tex.Sample(...)` — if the discard itself is conditional on a varying value, the samples in the surviving pixels may use incorrect mip levels derived from the discarded neighbours. The fix is to hoist the sample before the divergent branch, or to replace the implicit-gradient sample with an explicit `SampleGrad` or `SampleLevel` call with pre-computed gradients.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 14-23
// HIT(derivative-in-divergent-cf): implicit-gradient Sample inside
// a non-uniform branch — derivatives are undefined for masked-out lanes.
float4 ps_divergent_sample(float4 pos    : SV_Position,
                           float2 uv     : TEXCOORD0,
                           nointerpolation float mask : TEXCOORD1) : SV_Target {
    if (mask > 0.5) {
        // 'mask' is per-vertex varying despite nointerpolation; the branch is
        // non-uniform across the quad. Implicit-gradient Sample here is UB.
        return Tex.Sample(Bilinear, uv);
    }
    return float4(0, 0, 0, 1);
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 97-106
// HIT(derivative-in-divergent-cf): Sample with implicit gradients inside
// a branch driven by a per-pixel value — derivatives undefined on masked lanes.
float4 ps_deriv_in_mode_branch(float4 pos : SV_Position, float2 uv : TEXCOORD0,
                               float mask : TEXCOORD1) : SV_Target {
    float3 col = 0;
    if (mask > 0.0) {
        col = NoiseTex.Sample(BilinSS, uv).rgb;
    }
    return float4(col, 1.0);
}
```

### Good

```hlsl
// Option 1: hoist the sample before the branch.
float4 ps_divergent_sample_fixed(float4 pos    : SV_Position,
                                 float2 uv     : TEXCOORD0,
                                 float         mask : TEXCOORD1) : SV_Target {
    // Sample unconditionally while all quad lanes are active.
    float4 sampled = Tex.Sample(Bilinear, uv);
    if (mask > 0.5) {
        return sampled;
    }
    return float4(0, 0, 0, 1);
}

// Option 2: use explicit gradients computed before the branch.
float4 ps_divergent_sample_explicit_grad(float4 pos : SV_Position,
                                         float2 uv  : TEXCOORD0,
                                         float  mask : TEXCOORD1) : SV_Target {
    // Compute gradients in uniform control flow.
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    if (mask > 0.5) {
        // SampleGrad does not recompute derivatives — safe inside divergent CF.
        return Tex.SampleGrad(Bilinear, uv, dx, dy);
    }
    return float4(0, 0, 0, 1);
}
```

## Options

none

## Fix availability

**none** — Determining whether a sample is safe to hoist requires data-flow analysis across the full CFG to verify that no side-effects prevent moving the call, and that the texture and UV are already defined at the hoist point. The diagnostic explains the hazard and suggests the two fix strategies (hoist or pre-compute gradients), but the rewrite must be applied manually.

## See also

- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — the same divergent-CF family for compute barriers
- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — wave operations in divergent control flow
- Related rule: [discard-then-work](discard-then-work.md) — implicit-gradient hazard after `discard`
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — helper-lane participation in wave ops
- HLSL intrinsic reference: `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, `ddx_coarse`, `ddy_coarse` in the DirectX HLSL Intrinsics documentation
- DirectX Specification: DXIL specification section on derivative operations and helper lanes
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/derivative-in-divergent-cf.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
