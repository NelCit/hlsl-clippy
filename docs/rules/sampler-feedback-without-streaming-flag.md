---
id: sampler-feedback-without-streaming-flag
category: sampler-feedback
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# sampler-feedback-without-streaming-flag

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A shader that calls `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` against a `FeedbackTexture2D` / `FeedbackTexture2DArray` resource where reflection cannot find a corresponding tiled-resource (`Texture2D` with reserved/tiled binding flags, or a tier-2 sampler-feedback paired resource) attached to the same logical surface. The detector enumerates feedback-texture bindings via reflection and looks for the paired streaming-source binding metadata; it fires when the feedback writes appear without a streaming binding evidence.

## Why it matters on a GPU

Sampler feedback (DirectX 12 Ultimate, SM 6.5+) is a hardware mechanism that records which mip and tile of a logical texture were *actually* sampled by a shader, into a small companion feedback surface. The intended consumer of that feedback is a streaming subsystem: the CPU reads the feedback texture, decides which tiles to page in or out, and updates the tiled-resource backing of the source texture for subsequent frames. The hardware path on AMD RDNA 2/3 (`MIN_MIP` and `MIP_REGION_USED` feedback formats), NVIDIA Turing/Ada (sampler feedback maps), and Intel Xe-HPG involves dedicated bookkeeping in the TMU to materialise the feedback tile and writes it into the feedback resource at every sample.

When the application writes sampler feedback but never consumes it through a streaming system — e.g. the feedback resource is bound but the source texture is a normal committed `Texture2D` — the entire bookkeeping path runs for nothing. The TMU still pays the per-sample feedback materialisation cost, the feedback resource still consumes memory bandwidth on the writes, and on RDNA 2/3 specifically the feedback-write path uses a dedicated cache region that competes with the rest of the TMU's working set. Across a fullscreen pass that samples a textured surface every pixel, that's millions of dead feedback writes per frame.

The rule fires when the feedback writes are present but the streaming evidence (paired tiled resource, paired update bookkeeping) is missing from reflection. Real streaming pipelines should not be flagged; the rule's heuristic is "feedback is being written but reflection sees no consumer for it."

## Examples

### Bad

```hlsl
Texture2D<float4>          Diffuse  : register(t0);  // committed, not tiled
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> DiffuseFeedback : register(u0);
SamplerState               LinearSampler : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    // Feedback writes have no streaming consumer — pure dead bandwidth.
    DiffuseFeedback.WriteSamplerFeedback(Diffuse, LinearSampler, uv);
    return Diffuse.Sample(LinearSampler, uv);
}
```

### Good

```hlsl
// Source is a tiled / reserved resource fed by a streaming system that
// consumes the feedback texture each frame.
Texture2D<float4>          Diffuse        : register(t0);  // tiled resource
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> DiffuseFeedback : register(u0);
SamplerState               LinearSampler  : register(s0);

float4 ps_main(float2 uv : TEXCOORD0) : SV_Target {
    DiffuseFeedback.WriteSamplerFeedback(Diffuse, LinearSampler, uv);
    return Diffuse.Sample(LinearSampler, uv);
}

// Or, if streaming is not in use, drop the feedback writes entirely.
```

## Options

none

## Fix availability

**suggestion** — Removing feedback writes is straightforward; adding a streaming pipeline is a multi-system change. The diagnostic identifies the orphan feedback writes; the author decides whether to drop them or wire up a consumer.

## See also

- Related rule: [mip-clamp-zero-on-mipped-texture](mip-clamp-zero-on-mipped-texture.md) — sampler state that disables mip filtering
- D3D12 reference: `FeedbackTexture2D`, `WriteSamplerFeedback`, sampler feedback in the DirectX 12 Ultimate documentation
- HLSL intrinsic reference: `WriteSamplerFeedback` in the DirectX HLSL Shader Model 6.5 documentation
- Companion blog post: [sampler-feedback overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/sampler-feedback-without-streaming-flag.md)

*© 2026 NelCit, CC-BY-4.0.*
