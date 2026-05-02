---
id: feedback-write-wrong-stage
category: sampler-feedback
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# feedback-write-wrong-stage

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

Calls to `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` from a shader stage other than pixel. The SM 6.5 sampler-feedback specification restricts these writes to the pixel-shader stage, where the implicit derivatives required by the underlying `Sample`-equivalent feedback footprint are well-defined. The rule reads the entry-point stage from Slang reflection and fires on any feedback-write call reachable from a vertex, hull, domain, geometry, compute, mesh, amplification, or any raytracing stage.

## Why it matters on a GPU

Sampler feedback is a hardware-tracked side channel: when a PS texture sample executes, the texture unit on Turing/Ada/RDNA 2-3/Xe-HPG records which mip level and which tile the sample touched into a feedback texture. The streaming system reads the feedback texture between frames to decide which tiles to page in. The mechanism is implemented inside the texture unit's footprint generator, which only runs on PS waves because PS is the only stage where the rasterizer hands the texture unit a full set of derivatives at quad granularity. Other stages either lack derivatives entirely (vertex, geometry, compute, raytracing) or have only the explicit-derivative pathway (`SampleGrad`).

Calling `WriteSamplerFeedback*` from a non-pixel stage is a hard validation failure: DXC issues an error and the PSO will not link. If the call somehow reaches a runtime that did not validate it (older SDKs, custom toolchains), the behaviour is undefined — on RDNA 2/3 the texture unit will service the feedback request but the recorded mip is whatever derivatives the wave happened to have, which is typically zero or garbage; on NVIDIA Turing/Ada the texture unit silently drops the write; on Intel Xe-HPG behaviour has historically been a driver crash. None of these failure modes is what the author wanted.

Surfacing this at lint time saves a round trip through PSO compile failure. The diagnostic names the entry point and the stage so the author can either move the work to a PS pass or drop the feedback call.

## Examples

### Bad

```hlsl
// Compute shader cannot write sampler feedback — derivatives are not defined.
RWTexture2D<float4> g_Output;
Texture2D<float4>   g_Albedo;
SamplerState        g_Sampler;
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> g_Feedback;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float2 uv = (tid.xy + 0.5f) / 1024.0f;
    float4 c  = g_Albedo.SampleLevel(g_Sampler, uv, 0);
    g_Feedback.WriteSamplerFeedbackLevel(g_Albedo, g_Sampler, uv, 0); // ERROR
    g_Output[tid.xy] = c;
}
```

### Good

```hlsl
// Pixel shader: derivatives are well-defined, feedback write is legal.
Texture2D<float4>   g_Albedo;
SamplerState        g_Sampler;
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> g_Feedback;

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    g_Feedback.WriteSamplerFeedback(g_Albedo, g_Sampler, uv);
    return g_Albedo.Sample(g_Sampler, uv);
}
```

## Options

none

## Fix availability

**none** — Moving the feedback write to a PS pass is a structural change the linter cannot make safely. The diagnostic explains the constraint and points to the offending call.

## See also

- Related rule: [feedback-every-sample](feedback-every-sample.md) — overuse of feedback writes inside PS loops
- Related rule: [sampler-feedback-without-streaming-flag](sampler-feedback-without-streaming-flag.md) — feedback write that is not consumed by streaming
- D3D12 specification: Sampler Feedback (SM 6.5) stage restrictions
- Companion blog post: [sampler-feedback overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/feedback-write-wrong-stage.md)

*© 2026 NelCit, CC-BY-4.0.*
