---
title: "samplecmp-vs-manual-compare"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: samplecmp-vs-manual-compare
---

# samplecmp-vs-manual-compare

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/samplecmp-vs-manual-compare.md](../rules/samplecmp-vs-manual-compare.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The TMU on all current GPU architectures (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG) includes a dedicated hardware comparison unit on the texture pipeline. `SampleCmp` with a `SamplerComparisonState` routes the reference depth into this unit, which performs a bilinear comparison across the 2x2 texel footprint in a single TMU issue. On RDNA 3, a `SampleCmp` completes in the same number of TMU cycles as a plain `Sample` for many formats, because the comparison is fused into the filter stage at no additional ALU cost. The output is a single float in [0, 1] representing the filtered shadow term.

## What the rule fires on

A pattern where a shader samples a depth or shadow texture with a standard `Sample` or `Load` call and then performs a scalar comparison (`<`, `>`, `<=`, `>=`) on the fetched depth value against a reference depth variable. The rule fires when both conditions hold: the sampled texture is a declared depth resource (reflection type `Texture2D<float>` or `Texture2D<float4>` bound as a shadow map), and the comparison result drives a branch or is used as a shadow weight without passing through `SampleCmp`. It does not fire on uses of `SampleCmp` or `SampleCmpLevelZero` directly, nor when the comparison is against a constant rather than a per-pixel reference depth.

See the [What it detects](../rules/samplecmp-vs-manual-compare.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/samplecmp-vs-manual-compare.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[samplecmp-vs-manual-compare.md -> Examples](../rules/samplecmp-vs-manual-compare.md#examples).

## See also

- [Rule page](../rules/samplecmp-vs-manual-compare.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
