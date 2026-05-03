---
title: "feedback-write-wrong-stage"
date: 2026-05-02
author: shader-clippy maintainers
category: sampler-feedback
tags: [hlsl, performance, sampler-feedback]
status: stub
related-rule: feedback-write-wrong-stage
---

# feedback-write-wrong-stage

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/feedback-write-wrong-stage.md](../rules/feedback-write-wrong-stage.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Sampler feedback is a hardware-tracked side channel: when a PS texture sample executes, the texture unit on Turing/Ada/RDNA 2-3/Xe-HPG records which mip level and which tile the sample touched into a feedback texture. The streaming system reads the feedback texture between frames to decide which tiles to page in. The mechanism is implemented inside the texture unit's footprint generator, which only runs on PS waves because PS is the only stage where the rasterizer hands the texture unit a full set of derivatives at quad granularity. Other stages either lack derivatives entirely (vertex, geometry, compute, raytracing) or have only the explicit-derivative pathway (`SampleGrad`).

## What the rule fires on

Calls to `WriteSamplerFeedback`, `WriteSamplerFeedbackBias`, `WriteSamplerFeedbackGrad`, or `WriteSamplerFeedbackLevel` from a shader stage other than pixel. The SM 6.5 sampler-feedback specification restricts these writes to the pixel-shader stage, where the implicit derivatives required by the underlying `Sample`-equivalent feedback footprint are well-defined. The rule reads the entry-point stage from Slang reflection and fires on any feedback-write call reachable from a vertex, hull, domain, geometry, compute, mesh, amplification, or any raytracing stage.

See the [What it detects](../rules/feedback-write-wrong-stage.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/feedback-write-wrong-stage.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[feedback-write-wrong-stage.md -> Examples](../rules/feedback-write-wrong-stage.md#examples).

## See also

- [Rule page](../rules/feedback-write-wrong-stage.md) -- canonical reference + change log.
- [sampler-feedback overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
