---
title: "vrs-without-perprimitive-or-screenspace-source"
date: 2026-05-02
author: hlsl-clippy maintainers
category: vrs
tags: [hlsl, performance, vrs]
status: stub
related-rule: vrs-without-perprimitive-or-screenspace-source
---

# vrs-without-perprimitive-or-screenspace-source

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/vrs-without-perprimitive-or-screenspace-source.md](../rules/vrs-without-perprimitive-or-screenspace-source.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

PS-emitted VRS rates without an upstream source are silently ignored on most IHVs (Turing+, RDNA 2+, Battlemage). The shading-rate signal needs a combiner pair: per-pixel + per-primitive (or per-pixel + screen-space). A PS that emits a rate without a peer signal is dead code from the rasterizer's perspective.

## What the rule fires on

A pixel-shader entry point that emits `SV_ShadingRate` but the source contains no upstream VRS source (`[earlydepthstencil]`, per-primitive coarse-rate hint, or screen-space VRS image).

See the [What it detects](../rules/vrs-without-perprimitive-or-screenspace-source.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/vrs-without-perprimitive-or-screenspace-source.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[vrs-without-perprimitive-or-screenspace-source.md -> Examples](../rules/vrs-without-perprimitive-or-screenspace-source.md#examples).

## See also

- [Rule page](../rules/vrs-without-perprimitive-or-screenspace-source.md) -- canonical reference + change log.
- [vrs overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
