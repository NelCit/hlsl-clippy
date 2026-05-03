---
id: redundant-unorm-snorm-conversion
category: math
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# redundant-unorm-snorm-conversion

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

An explicit fixed-point-to-float scaling expression — `* (1.0 / 255.0)`, `/ 255.0`, `* (1.0 / 65535.0)`, `* (1.0 / 127.0)`, `* (2.0 / 255.0) - 1.0`, and the literal-evaluated equivalents `* 0.00392156862745098` and similar — applied to the result of a texture `Sample`, `Load`, or `Gather` call, or to a value just unpacked from a packed integer source. The rule matches the literal scaling factors that uniquely identify UNORM (`1/255`, `1/65535`) and SNORM (`2/255 − 1`, `1/127`) decoding patterns. It is purely AST-driven on the literal: the Phase 2 rule does not look at the resource binding to confirm the texture is actually UNORM/SNORM-formatted, so it can fire on the rare case where the source is an integer view that genuinely needs the explicit scale. A reflection-aware tightening that confirms the source format is filed as a Phase 3 follow-up in ADR 0011.

## Why it matters on a GPU

A UNORM texture format (`R8_UNORM`, `R8G8B8A8_UNORM`, `R16_UNORM`, …) carries an explicit hardware contract: every sampling operation returns a 32-bit float already normalised to the `[0, 1]` range. The conversion happens in fixed-function silicon — on AMD RDNA 2/3 it is folded into the texture filter unit's output formatter, on NVIDIA Turing and Ada Lovelace it is the texture-pipeline's address-mode-and-format converter that runs in parallel with the LERP unit, on Intel Xe-HPG it is the sampler's data-port conversion stage. The cost is zero ALU because no ALU runs: the conversion is a side-effect of the load that the texture unit performs whether the shader asks for it or not. SNORM works the same way except the format converter sign-extends and remaps to `[-1, 1]`.

When the shader then writes `tex.Sample(...).r * (1.0 / 255.0)`, the texture unit has already produced a float in `[0, 1]` — it is *the same float* the explicit divide is trying to produce — and the explicit scale runs over the top. That extra `v_mul_f32` (RDNA), `FMUL` (Turing/Ada), or EU multiply (Xe-HPG) is one VALU per component per sampled pixel, on a code path that is hot by definition (texture sampling is rarely the cold path). Worse, it changes the numbers: the true UNORM-to-float identity is `value / 255.0` only for `R8`; the texture unit's actual conversion is bit-exact UNORM (`v / (2^N - 1)` for an N-bit channel), which the floating-point divide-by-255 reproduces for `R8` only by coincidence of the literal. For `R16_UNORM` the same anti-pattern with `* (1.0 / 65535.0)` divides the already-normalised result a second time and produces values around `1 / 65535` of the correct magnitude — a silent magnitude bug that is easy to miss until the texture goes black on screen.

The pattern is endemic in code ported from CPU-side decoders, from older fixed-function pipelines that exposed integer texture loads, and from compute shaders that originally read raw bytes via `ByteAddressBuffer` and were then refactored to use a UNORM `Texture2D`. The refactor often updates the binding but leaves the explicit divide behind, and the original author (who knew the format was integer) is no longer the reader. The fix is to drop the multiply or divide entirely; the texture unit's free conversion does the work the explicit math was reproducing at ALU cost.

## Examples

### Bad

```hlsl
// UNORM sample already returns [0, 1]; the divide is dead arithmetic and
// (on R16_UNORM ports) a silent magnitude bug.
float4 ps_albedo(float2 uv : TEXCOORD0) : SV_Target {
    float4 raw = AlbedoUnorm.Sample(BilinearWrap, uv);
    return raw * (1.0 / 255.0);
}

// SNORM equivalent: the texture unit already produced [-1, 1].
float3 ps_normal(float2 uv : TEXCOORD0) : SV_Target {
    float3 n = NormalSnorm.Sample(BilinearWrap, uv).rgb;
    return n * (2.0 / 255.0) - 1.0;
}
```

### Good

```hlsl
// The texture unit's format converter does the work for free.
float4 ps_albedo(float2 uv : TEXCOORD0) : SV_Target {
    return AlbedoUnorm.Sample(BilinearWrap, uv);
}

float3 ps_normal(float2 uv : TEXCOORD0) : SV_Target {
    return NormalSnorm.Sample(BilinearWrap, uv).rgb;
}
```

## Options

none

## Fix availability

**suggestion** — The candidate fix removes the trailing `* (1.0 / 255.0)` (or the SNORM remap pair) and leaves the bare sampling expression. The rewrite is shown as a suggestion because the Phase 2 detector matches purely on the literal scaling factor: it does not have reflection access to confirm the texture binding is actually `R8_UNORM` / `R16_UNORM` / `R8_SNORM`. A small number of valid programs sample a `Texture2D<uint>` integer view and apply the explicit scale deliberately — for those, the suggestion is wrong and the developer must reject it. The Phase 3 follow-up rule (filed in ADR 0011) tightens the detector to fire only when reflection confirms the bound resource is a normalised format, at which point the fix can be promoted to machine-applicable.

## See also

- Related rule: [pack-then-unpack-roundtrip](pack-then-unpack-roundtrip.md) — flags hand-written pack/unpack pairs that the format converter would do for free
- Related rule: [manual-f32tof16](manual-f32tof16.md) — surfaces the analogous half-float conversion anti-pattern
- Related rule: [unpack-then-repack](unpack-then-repack.md) — companion rule for round-trip pack/unpack of integer-typed buffer payloads
- HLSL reference: UNORM / SNORM sampling semantics, `Texture2D.Sample`, `Texture2D.Load` in the DirectX HLSL language reference
- DXGI format reference: `DXGI_FORMAT_R8_UNORM`, `DXGI_FORMAT_R8G8B8A8_UNORM`, `DXGI_FORMAT_R16_UNORM`, `DXGI_FORMAT_R8_SNORM`
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/redundant-unorm-snorm-conversion.md)

*© 2026 NelCit, CC-BY-4.0.*
