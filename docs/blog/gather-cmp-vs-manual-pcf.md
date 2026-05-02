---
title: "gather-cmp-vs-manual-pcf: A 2x2 grid of `SampleCmp` or `SampleCmpLevelZero` calls whose UV arguments differ by constant…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: gather-cmp-vs-manual-pcf
---

# gather-cmp-vs-manual-pcf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/gather-cmp-vs-manual-pcf.md](../rules/gather-cmp-vs-manual-pcf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`GatherCmp` is the dedicated TMU instruction for exactly this pattern. A single `GatherCmp(cmpSampler, uv, refDepth)` call fetches the four texels in the 2x2 bilinear footprint surrounding the UV coordinate, performs the comparison against `refDepth` for each of the four texels in TMU hardware, and returns the four binary (or filtered) comparison results packed into a `float4`. This is one TMU instruction issued in one cycle on AMD RDNA 2/3 and NVIDIA Turing/Ada hardware.

## What the rule fires on

A 2x2 grid of `SampleCmp` or `SampleCmpLevelZero` calls whose UV arguments differ by constant per-texel offsets of (0,0), (1,0), (0,1), and (1,1) in texel space, whose results are blended together with lerp or manual weights. This pattern is the classic software implementation of 2x2 percentage-closer filtering (PCF). The rule fires when all four `SampleCmp` calls share the same shadow-map resource and the same reference depth, and the offset pattern is recognisable from the UV arguments. It does not fire when fewer than four samples are present, when the offsets are non-rectangular, or when the result is used without blending (for example, when the four values are summed and divided by four, which is also a valid PCF form and still triggers the suggestion).

See the [What it detects](../rules/gather-cmp-vs-manual-pcf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/gather-cmp-vs-manual-pcf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[gather-cmp-vs-manual-pcf.md -> Examples](../rules/gather-cmp-vs-manual-pcf.md#examples).

## See also

- [Rule page](../rules/gather-cmp-vs-manual-pcf.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
