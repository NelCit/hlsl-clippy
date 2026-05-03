---
title: "comparison-sampler-without-comparison-op"
date: 2026-05-02
author: shader-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: comparison-sampler-without-comparison-op
---

# comparison-sampler-without-comparison-op

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/comparison-sampler-without-comparison-op.md](../rules/comparison-sampler-without-comparison-op.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`SamplerComparisonState` is a distinct descriptor type from `SamplerState` on every modern API and IHV. It carries a `ComparisonFunc` field (`LESS`, `LESS_EQUAL`, `GREATER`, etc.) that drives a hardware comparison-and-blend path inside the sampler unit. On AMD RDNA 2/3 the TMU has a dedicated PCF (percentage-closer filtering) hardware path that, given a comparison sampler, performs four texel comparisons against the reference value and returns a blended in-shadow / out-of-shadow ratio in one sampler-unit cycle. NVIDIA Turing/Ada and Intel Xe-HPG document equivalent comparison hardware. The descriptor's `ComparisonFunc` field is *only* consumed by the `SampleCmp*` and `GatherCmp*` methods; non-`Cmp` calls ignore the field entirely.

## What the rule fires on

A `SamplerComparisonState` declaration that, across all reflection-visible call sites against textures bound with this sampler, is only used with non-`Cmp`-suffixed sample methods (`Sample`, `SampleLevel`, `SampleGrad`, `SampleBias`) and never with the comparison-suffixed variants (`SampleCmp`, `SampleCmpLevelZero`, `SampleCmpLevel`, `GatherCmp`). The detector enumerates sampler bindings via reflection, finds every `Sample*` call against textures that reference each comparison sampler, and fires when no call site uses a comparison method. It does not fire when at least one call site uses a `Cmp` variant against the sampler.

See the [What it detects](../rules/comparison-sampler-without-comparison-op.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/comparison-sampler-without-comparison-op.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[comparison-sampler-without-comparison-op.md -> Examples](../rules/comparison-sampler-without-comparison-op.md#examples).

## See also

- [Rule page](../rules/comparison-sampler-without-comparison-op.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
