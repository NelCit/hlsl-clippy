---
id: wave-intrinsic-helper-lane-hazard
category: control-flow
severity: warn
applicability: none
since-version: v0.5.0
phase: 4
---

# wave-intrinsic-helper-lane-hazard

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Pixel shaders that call any cross-lane wave intrinsic (`WaveActiveSum`, `WaveActiveMin`, `WavePrefixSum`, `WaveActiveBallot`, `WaveReadLaneAt`, etc.) on a code path that is reachable after a `discard` or `clip` may have executed, when the entry point does not opt out of helper-lane participation via SM 6.6 `IsHelperLane()` guards. The same hazard applies to wave intrinsics placed inside loops where some quad lanes have early-exited: the inactive lanes are still tracked as helper lanes and may participate in cross-lane operations, contributing values that the algorithm did not intend to include.

## Why it matters on a GPU

The pixel-shader execution model on every modern GPU keeps the four lanes of a 2x2 quad co-resident even when some lanes are not part of a covered triangle. The non-covered lanes are called helper lanes; their results are discarded at the end, but they execute every instruction so that derivatives (`ddx`, `ddy`, implicit-LOD `Sample`) can read consistent coordinate values from all four lanes. This is essential for correct mip selection and gradient-based shading. After a `discard`, the affected lane likewise becomes a helper: it continues executing for derivative consistency but its colour and depth output are dropped.

Wave intrinsics span the entire wave (32 or 64 lanes on AMD RDNA, 32 lanes on NVIDIA Turing/Ada/Blackwell, 8/16/32 on Intel Xe-HPG), not just the quad. By default they include helper lanes in the active set. This means `WaveActiveSum(value)` after a `discard` sums contributions from lanes that were supposed to be culled — typically with stale or default-initialised values — which produces wrong totals. `WaveActiveBallot` over an `if (visible)` mask in the presence of helpers similarly returns ballots that count unkillable pixels as having voted. On AMD RDNA, the helper lanes are tracked via the `EXEC` mask and a separate helper-lane bit; on NVIDIA, the `_helper_invocation` bit per warp lane plays the same role; on Intel Xe-HPG, the `EMASK` register tracks helpers explicitly.

SM 6.6 introduced the `IsHelperLane()` intrinsic precisely to let shader authors gate cross-lane operations against helper participation: `if (!IsHelperLane()) { reduce... }` produces a wave intrinsic call that runs only on real lanes. On older shader models, the same intent must be expressed via `WaveActiveBallot(!IsHelperLane())` plus manual `countbits` and `WaveReadLaneAt` work, which is verbose and error-prone. The hazard is loudest in screen-space resolves (TAA, SSR, denoisers) that combine alpha-test materials with wave-level reductions: the helper-lane contamination biases every sum, average, and prefix scan across the wave by an amount that varies with screen content.

## Examples

### Bad

```hlsl
Texture2D    AlphaMask : register(t0);
SamplerState Samp      : register(s0);

float4 ps_wave_after_discard(float2 uv : TEXCOORD0,
                              float3 colour : COLOR0) : SV_Target {
    float a = AlphaMask.Sample(Samp, uv).a;
    if (a < 0.5) discard;
    // Helper lanes from neighbouring quads (and from this quad if any
    // pixel was discarded) still participate in WaveActiveSum.
    float wave_avg = WaveActiveSum(colour.r) / WaveActiveCountBits(true);
    return float4(colour.rgb * wave_avg, 1);
}
```

### Good

```hlsl
// SM 6.6: gate the wave reduction with IsHelperLane() so helpers do not
// contribute their stale colour values.
float4 ps_wave_after_discard_fixed(float2 uv : TEXCOORD0,
                                    float3 colour : COLOR0) : SV_Target {
    float a = AlphaMask.Sample(Samp, uv).a;
    if (a < 0.5) discard;
    bool real = !IsHelperLane();
    float sum = WaveActiveSum(real ? colour.r : 0.0);
    uint  cnt = WaveActiveCountBits(real);
    float wave_avg = sum / max(1u, cnt);
    return float4(colour.rgb * wave_avg, 1);
}
```

## Options

none

## Fix availability

**none** — Adding helper-lane guards changes the participating set of a wave reduction. Whether the change matches the algorithm's intent is a semantic question and cannot be resolved by textual rewriting. The diagnostic identifies the wave intrinsic and the preceding `discard` site.

## See also

- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — wave intrinsics in divergent control flow
- Related rule: [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md) — `discard` and early-Z interaction
- Related rule: [sample-in-loop-implicit-grad](sample-in-loop-implicit-grad.md) — derivative correctness with helper lanes
- HLSL intrinsic reference: `IsHelperLane`, `WaveActiveSum`, `WaveActiveBallot` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wave-intrinsic-helper-lane-hazard.md)

*© 2026 NelCit, CC-BY-4.0.*
