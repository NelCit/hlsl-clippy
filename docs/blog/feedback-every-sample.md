---
title: "feedback-every-sample"
date: 2026-05-02
author: shader-clippy maintainers
category: sampler-feedback
tags: [hlsl, performance, sampler-feedback]
status: stub
related-rule: feedback-every-sample
---

# feedback-every-sample

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/feedback-every-sample.md](../rules/feedback-every-sample.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Sampler feedback exists to drive streaming systems: the GPU records which mip levels and tile regions of which textures were actually sampled by the shader, and the streaming system uses that aggregate to decide what to load and what to evict. The feedback target is a small (typically 1/8 or 1/16 the spatial resolution of the source texture, encoded as a packed u8 mip-mask per tile) write-only view that the hardware updates with bitwise OR semantics. On AMD RDNA 2/3, `WriteSamplerFeedback` is implemented as a tile-coordinate computation followed by a write to the feedback resource through a dedicated path that bypasses the normal colour write logic. On NVIDIA Turing+ (sampler feedback was added to the DX12 spec in 2020 and supported on Turing/Ada with driver updates), the same write path is dedicated. On Intel Xe-HPG, sampler feedback is supported through the LSC pipe with explicit tile-residency tracking.

## What the rule fires on

Calls to `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` placed unconditionally in the hot path of a pixel shader (the main per-pixel material body) without a stochastic gate that discards the vast majority of writes. The Microsoft sampler-feedback specification explicitly recommends sampling the feedback at no more than 1-2% of pixels per frame; an unconditional `WriteSamplerFeedback*` per pixel both wastes the feedback unit's bandwidth and overwrites useful aggregate data with redundant writes from spatially-adjacent pixels.

See the [What it detects](../rules/feedback-every-sample.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/feedback-every-sample.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[feedback-every-sample.md -> Examples](../rules/feedback-every-sample.md#examples).

## See also

- [Rule page](../rules/feedback-every-sample.md) -- canonical reference + change log.
- [sampler-feedback overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
