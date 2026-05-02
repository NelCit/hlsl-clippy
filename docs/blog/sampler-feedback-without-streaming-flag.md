---
title: "sampler-feedback-without-streaming-flag"
date: 2026-05-02
author: hlsl-clippy maintainers
category: sampler-feedback
tags: [hlsl, performance, sampler-feedback]
status: stub
related-rule: sampler-feedback-without-streaming-flag
---

# sampler-feedback-without-streaming-flag

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sampler-feedback-without-streaming-flag.md](../rules/sampler-feedback-without-streaming-flag.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Sampler feedback (DirectX 12 Ultimate, SM 6.5+) is a hardware mechanism that records which mip and tile of a logical texture were *actually* sampled by a shader, into a small companion feedback surface. The intended consumer of that feedback is a streaming subsystem: the CPU reads the feedback texture, decides which tiles to page in or out, and updates the tiled-resource backing of the source texture for subsequent frames. The hardware path on AMD RDNA 2/3 (`MIN_MIP` and `MIP_REGION_USED` feedback formats), NVIDIA Turing/Ada (sampler feedback maps), and Intel Xe-HPG involves dedicated bookkeeping in the TMU to materialise the feedback tile and writes it into the feedback resource at every sample.

## What the rule fires on

A shader that calls `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` against a `FeedbackTexture2D` / `FeedbackTexture2DArray` resource where reflection cannot find a corresponding tiled-resource (`Texture2D` with reserved/tiled binding flags, or a tier-2 sampler-feedback paired resource) attached to the same logical surface. The detector enumerates feedback-texture bindings via reflection and looks for the paired streaming-source binding metadata; it fires when the feedback writes appear without a streaming binding evidence.

See the [What it detects](../rules/sampler-feedback-without-streaming-flag.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sampler-feedback-without-streaming-flag.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sampler-feedback-without-streaming-flag.md -> Examples](../rules/sampler-feedback-without-streaming-flag.md#examples).

## See also

- [Rule page](../rules/sampler-feedback-without-streaming-flag.md) -- canonical reference + change log.
- [sampler-feedback overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
