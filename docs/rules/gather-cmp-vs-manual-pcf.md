---
id: gather-cmp-vs-manual-pcf
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# gather-cmp-vs-manual-pcf

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

A 2x2 grid of `SampleCmp` or `SampleCmpLevelZero` calls whose UV arguments differ by constant per-texel offsets of (0,0), (1,0), (0,1), and (1,1) in texel space, whose results are blended together with lerp or manual weights. This pattern is the classic software implementation of 2x2 percentage-closer filtering (PCF). The rule fires when all four `SampleCmp` calls share the same shadow-map resource and the same reference depth, and the offset pattern is recognisable from the UV arguments. It does not fire when fewer than four samples are present, when the offsets are non-rectangular, or when the result is used without blending (for example, when the four values are summed and divided by four, which is also a valid PCF form and still triggers the suggestion).

## Why it matters on a GPU

`GatherCmp` is the dedicated TMU instruction for exactly this pattern. A single `GatherCmp(cmpSampler, uv, refDepth)` call fetches the four texels in the 2x2 bilinear footprint surrounding the UV coordinate, performs the comparison against `refDepth` for each of the four texels in TMU hardware, and returns the four binary (or filtered) comparison results packed into a `float4`. This is one TMU instruction issued in one cycle on AMD RDNA 2/3 and NVIDIA Turing/Ada hardware.

The manual 2x2 PCF pattern — four `SampleCmp` calls with explicit UV offsets — costs four separate TMU instructions. Each `SampleCmp` is an independent issue, and the four-wide gather footprint is not reused across them. On RDNA 3 at typical TMU issue rates (one texture operation per cycle per TMU), this means the manual PCF loop is approximately four times as expensive in TMU cycles as the `GatherCmp` equivalent. On NVIDIA Turing, `SampleCmp` runs at approximately quarter-rate throughput on certain shadow formats, and four serial `SampleCmp` calls in a pixel shader can stall the quad for 16+ cycles. `GatherCmp` on the same hardware runs at half-rate, making the gather form roughly two times cheaper than the manual loop.

After `GatherCmp` returns the four comparison results in a `float4`, the shader must still apply the bilinear blend weights manually using `frac(uv * textureSize)`. This is a small amount of arithmetic (four multiplications and a lerp) that runs at full VALU rate and has negligible cost compared to the four TMU instructions it replaces. The fix is mechanical: compute the base UV, issue one `GatherCmp`, extract the four corners from the returned `float4`, and reconstruct the bilinear filter using `lerp`. `hlsl-clippy fix` emits this pattern automatically.

## Examples

### Bad

```hlsl
// Manual 2x2 PCF — four SampleCmpLevelZero calls with per-texel UV offsets.
Texture2D              ShadowMap : register(t0);
SamplerComparisonState ShadowCmp : register(s0);

cbuffer ShadowCB { float2 ShadowTexelSize; };

float pcf_2x2_manual(float2 uv, float refDepth) {
    float s00 = ShadowMap.SampleCmpLevelZero(ShadowCmp, uv,                              refDepth);
    float s10 = ShadowMap.SampleCmpLevelZero(ShadowCmp, uv + float2(ShadowTexelSize.x, 0), refDepth);
    float s01 = ShadowMap.SampleCmpLevelZero(ShadowCmp, uv + float2(0, ShadowTexelSize.y), refDepth);
    float s11 = ShadowMap.SampleCmpLevelZero(ShadowCmp, uv + ShadowTexelSize,              refDepth);

    float2 f   = frac(uv / ShadowTexelSize);
    float  top = lerp(s00, s10, f.x);
    float  bot = lerp(s01, s11, f.x);
    return lerp(top, bot, f.y);
}
```

### Good

```hlsl
// After machine-applicable fix: one GatherCmp replaces four SampleCmpLevelZero calls.
float pcf_2x2_gather(float2 uv, float refDepth) {
    // GatherCmp fetches the 2x2 footprint and compares all four texels in one TMU op.
    float4 cmp = ShadowMap.GatherCmp(ShadowCmp, uv, refDepth);
    // cmp.x = (0,1), cmp.y = (1,1), cmp.z = (1,0), cmp.w = (0,0) — HLSL gather order.

    float2 f   = frac(uv / ShadowTexelSize);
    float  top = lerp(cmp.w, cmp.z, f.x);   // bottom row: (0,0) and (1,0)
    float  bot = lerp(cmp.x, cmp.y, f.x);   // top row:    (0,1) and (1,1)
    return lerp(top, bot, f.y);
}
```

## Options

none

## Fix availability

**suggestion** — The end-state rewrite (one `GatherCmp` plus a `lerp`-based bilinear reconstruction) is semantically equivalent to the source 2x2 PCF, but the rewrite is *not* safely automatable in the current detector for three reasons:

1. **Multi-statement, multi-variable rewrite.** The fix has to delete the four `SampleCmp*` calls and their named results (`s00`/`s10`/`s01`/`s11` in the canonical example), insert a single `GatherCmp` call, and rewrite the downstream `lerp` math against the new component-order convention (`cmp.x` = (0,1), `cmp.y` = (1,1), `cmp.z` = (1,0), `cmp.w` = (0,0) per the HLSL spec). The current detector reports a *cluster* of `SampleCmp*` calls in a small source window without verifying the offset pattern or identifying the downstream blend variables, so a mechanical rewrite would have to invent identifier names and is not provably correct on arbitrary call shapes.
2. **Texel-size dependency must be supplied.** Reconstructing the bilinear filter requires `frac(uv * textureSize)` (or `frac(uv / texelSize)`); the detector cannot derive the texel-size constant from source alone — it lives in a cbuffer field whose name varies per project. A machine-applicable rewrite would need a project-wide convention (e.g. always `ShadowTexelSize`) that the rule does not enforce.
3. **Per-tap UV-offset shape varies.** Real-world PCF kernels include 3x3 and 4x4 variants, gaussian-weighted variants, and rotated-disc variants. The rule fires on >=4 calls in a window as a coarse heuristic; collapsing to `GatherCmp` is only correct for the 2x2 axis-aligned pattern, and the detector does not currently prove the input is that shape.

The lint surfaces the perf opportunity; the rewrite needs the developer to confirm the kernel shape and supply the texel-size source. If you want the rule to emit a `GatherCmp`-based code action you can review-and-apply by hand, that is tracked as a follow-up.

## See also

- Related rule: [`samplecmp-vs-manual-compare`](samplecmp-vs-manual-compare.md) — single-texel hand-rolled depth compare should use `SampleCmp`
- Related rule: [`gather-channel-narrowing`](gather-channel-narrowing.md) — single-channel gather pattern uses `GatherRed` / `GatherGreen`
- Related rule: [`missing-precise-on-pcf`](missing-precise-on-pcf.md) — PCF depth arithmetic without `precise` is a shadow acne risk
- HLSL intrinsic reference: `Texture2D.GatherCmp`, `Texture2D.SampleCmpLevelZero` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/gather-cmp-vs-manual-pcf.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
