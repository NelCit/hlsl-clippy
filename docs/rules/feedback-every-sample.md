---
id: feedback-every-sample
category: sampler-feedback
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# feedback-every-sample

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls to `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` placed unconditionally in the hot path of a pixel shader (the main per-pixel material body) without a stochastic gate that discards the vast majority of writes. The Microsoft sampler-feedback specification explicitly recommends sampling the feedback at no more than 1-2% of pixels per frame; an unconditional `WriteSamplerFeedback*` per pixel both wastes the feedback unit's bandwidth and overwrites useful aggregate data with redundant writes from spatially-adjacent pixels.

## Why it matters on a GPU

Sampler feedback exists to drive streaming systems: the GPU records which mip levels and tile regions of which textures were actually sampled by the shader, and the streaming system uses that aggregate to decide what to load and what to evict. The feedback target is a small (typically 1/8 or 1/16 the spatial resolution of the source texture, encoded as a packed u8 mip-mask per tile) write-only view that the hardware updates with bitwise OR semantics. On AMD RDNA 2/3, `WriteSamplerFeedback` is implemented as a tile-coordinate computation followed by a write to the feedback resource through a dedicated path that bypasses the normal colour write logic. On NVIDIA Turing+ (sampler feedback was added to the DX12 spec in 2020 and supported on Turing/Ada with driver updates), the same write path is dedicated. On Intel Xe-HPG, sampler feedback is supported through the LSC pipe with explicit tile-residency tracking.

The cost per write is small per call (a few cycles), but a 1080p shader that writes feedback for every pixel issues 2 million feedback writes per frame per material — at 60 Hz, that is 120 million feedback writes per second per pass. Across a typical AAA scene with 8-12 material passes, this approaches the bandwidth budget for the feedback resource itself, and provides no benefit because feedback is already coarse-grained: the entire 4x4 or 8x8 pixel block maps to the same feedback tile, so writing 16-64 times per tile per frame produces the same result as writing once. The Microsoft sampler feedback documentation states explicitly that callers should write feedback for "a small fraction of pixels — for example 1 in 64" per frame, using a stochastic mask based on `SV_Position` or a frame counter.

The fix is to gate the call with a sub-pixel mask. A common pattern is `if ((uint(SV_Position.x) ^ uint(SV_Position.y) ^ frame_id) & 0x3F) == 0` to write 1 in 64 pixels with good spatial-temporal coverage. For workloads that need sub-frame latency (e.g., user-visible LOD pop reduction), a 1-in-16 gate is acceptable. The gating is essentially free — one xor, one and, one compare — and recovers the entire feedback-write budget. Several engines (UE5, Frostbite) ship sampler-feedback writes gated this way by default; manual writes outside the engine framework are the typical source of unguarded feedback writes.

## Examples

### Bad

```hlsl
Texture2D                       Albedo       : register(t0);
SamplerState                    Samp         : register(s0);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIP_REGION_USED>
                                AlbedoFB     : register(u0);

float4 ps_unconditional_feedback(float4 sv_pos : SV_Position,
                                  float2 uv     : TEXCOORD0) : SV_Target {
    // Feedback written every pixel — spatially-adjacent pixels overwrite
    // each other's tile bits, wasting bandwidth and saturating the
    // feedback unit.
    AlbedoFB.WriteSamplerFeedback(Albedo, Samp, uv);
    return Albedo.Sample(Samp, uv);
}
```

### Good

```hlsl
cbuffer PerFrame : register(b0) { uint g_FrameId; };

float4 ps_gated_feedback(float4 sv_pos : SV_Position,
                          float2 uv     : TEXCOORD0) : SV_Target {
    uint x = (uint)sv_pos.x;
    uint y = (uint)sv_pos.y;
    // Bayer-style stochastic gate: writes 1 in 64 pixels per frame, with
    // good spatial and temporal coverage across frames.
    if (((x ^ y ^ g_FrameId) & 0x3Fu) == 0u) {
        AlbedoFB.WriteSamplerFeedback(Albedo, Samp, uv);
    }
    return Albedo.Sample(Samp, uv);
}
```

## Options

- `gate-fraction` (number, default: 0.015625) — the recommended fraction of pixels that should write feedback. Values in `[1/256, 1/8]` are typical.

## Fix availability

**suggestion** — Adding a stochastic gate changes the rate at which feedback is collected and may alter the streaming system's tuning. The diagnostic flags the unguarded write and proposes the gate template.

## See also

- Microsoft DirectX docs: Sampler Feedback — `FeedbackTexture2D`, `WriteSamplerFeedback*`
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/feedback-every-sample.md)

*© 2026 NelCit, CC-BY-4.0.*
