---
title: "vrs-rate-conflict-with-target"
date: 2026-05-02
author: shader-clippy maintainers
category: vrs
tags: [hlsl, performance, vrs]
status: stub
related-rule: vrs-rate-conflict-with-target
---

# vrs-rate-conflict-with-target

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/vrs-rate-conflict-with-target.md](../rules/vrs-rate-conflict-with-target.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12 / Vulkan VRS rate combiners produce the *minimum* of per-primitive and per-pixel rates -- conflicting declarations silently override the author's expectation. When the per-primitive rate is coarser, the per-pixel rate is ignored on Turing+/Ampere/Ada/Battlemage; when the per-pixel rate is coarser, the per-primitive rate dominates. The author who sets both rarely intends both.

## What the rule fires on

A pixel shader that emits `SV_ShadingRate` when the source also declares a per-primitive coarse-rate marker (e.g. `D3D12_SHADING_RATE_COMBINER`, `PerPrimitive`, `CoarseShadingRate`).

See the [What it detects](../rules/vrs-rate-conflict-with-target.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/vrs-rate-conflict-with-target.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[vrs-rate-conflict-with-target.md -> Examples](../rules/vrs-rate-conflict-with-target.md#examples).

## See also

- [Rule page](../rules/vrs-rate-conflict-with-target.md) -- canonical reference + change log.
- [vrs overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
