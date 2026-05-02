---
id: manual-srgb-conversion
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# manual-srgb-conversion

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A hand-rolled gamma 2.2 or sRGB transfer function (`pow(c, 2.2)`, `pow(c.rgb, 2.2)`, `pow(c, 1.0/2.2)`, the canonical piecewise sRGB inverse, or the constant-folded expansion thereof) applied to the result of a `Sample`/`Load` call against a resource whose format reflection reports as one of `DXGI_FORMAT_*_SRGB` (e.g. `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`, `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB`, `DXGI_FORMAT_BC1_UNORM_SRGB` ... `BC7_UNORM_SRGB`). The detector pattern-matches the gamma-curve expression on the AST side and cross-references the sampled resource's format on the reflection side. It does not fire on samples from non-sRGB resources (where the manual conversion may be the correct conversion).

## Why it matters on a GPU

`*_SRGB` texture formats on every modern GPU IHV invoke a hardware sRGB-to-linear converter on every texel fetch. AMD RDNA 2/3 documents the converter as part of the TMU's format-decode pipeline; NVIDIA Turing/Ada and Intel Xe-HPG provide equivalent hardware. The hardware converter runs for free on the sample path — there is no shader-cycle cost — and it implements the exact piecewise sRGB curve from the IEC 61966-2-1 specification, with sub-bit precision better than what a 32-bit `pow(c, 2.2)` can achieve given the precision loss of the transcendental.

When the shader applies a manual gamma conversion on top of a value the hardware has already linearised, the result is double-converted: a texel stored as 0.5 in sRGB is converted to ~0.215 in linear by the hardware, then `pow(0.215, 2.2)` further darkens it to ~0.034. The visual regression is "everything looks too dark" — particularly visible in mid-tones — and the cause is buried in the format-vs-shader-math interaction. The rule is the most common bug seen during a `R8G8B8A8_UNORM` -> `R8G8B8A8_UNORM_SRGB` migration: the engine flips the format to take advantage of the hardware converter, but the shader retains the legacy manual conversion.

The performance cost is two-fold: the manual `pow` is a transcendental that issues at 1/4 VALU rate on RDNA 3 and through the multi-function unit on Ada (a few cycles per call versus zero for the hardware path), and the doubled conversion flow makes any subsequent linear-space math compound the wrong values. Removing the manual conversion is both a correctness fix and a small ALU saving.

## Examples

### Bad

```hlsl
Texture2D<float4> Albedo        : register(t0);  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
SamplerState      LinearSampler : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    float4 c = Albedo.Sample(LinearSampler, uv);
    // Hardware already linearised c; this re-applies the curve.
    c.rgb = pow(c.rgb, 2.2);
    return c;
}
```

### Good

```hlsl
Texture2D<float4> Albedo        : register(t0);  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
SamplerState      LinearSampler : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    // Sample is already linear; trust the format conversion.
    return Albedo.Sample(LinearSampler, uv);
}
```

## Options

none

## Fix availability

**suggestion** — Removing the gamma call changes the visible output (the curve was masking some other tuning). The diagnostic identifies the doubled conversion; the author confirms the upstream tuning before stripping the call.

The v1.2 foundation (ADR 0019) wires this rule to the new `ResourceBinding::dxgi_format` field: when reflection surfaces an SRGB-suffixed format string for any bound texture, the rule fires; otherwise it stays silent. The Slang 2026.7.1 ABI does not surface the SRGB qualifier through `TypeReflection::getName()`, so today `dxgi_format` is empty for SRGB textures in practice and the diagnostic is gated off. The probe is forward-compatible — when a future Slang surfaces the qualifier, this rule lights up with no further code change. The fix stays suggestion-only because the rewrite (drop the `pow(x, 2.2)` call and replace with `x`) requires confirming that no upstream tuning compensates for the doubled curve.

## See also

- Related rule: [bgra-rgba-swizzle-mismatch](bgra-rgba-swizzle-mismatch.md) — companion format-vs-shader-math mismatch
- Related rule: [redundant-unorm-snorm-conversion](redundant-unorm-snorm-conversion.md) — explicit `* (1.0/255.0)` after a UNORM sample
- DXGI reference: `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` and the sRGB conversion semantics table
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/manual-srgb-conversion.md)

*© 2026 NelCit, CC-BY-4.0.*
