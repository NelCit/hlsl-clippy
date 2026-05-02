---
title: "early-z-disabled-by-conditional-discard"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: early-z-disabled-by-conditional-discard
---

# early-z-disabled-by-conditional-discard

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/early-z-disabled-by-conditional-discard.md](../rules/early-z-disabled-by-conditional-discard.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Modern GPUs perform depth/stencil testing at two possible points in the pipeline: before pixel shading (early-Z) or after pixel shading (late-Z). Early-Z is a major performance win: the depth comparison happens in fixed-function hardware that can reject hundreds of fragments per clock, before any shader work, before texture fetches, before any VGPR is allocated. Late-Z runs after the pixel shader has fully executed and can only update the depth buffer at the very end, after committing colour. The cost difference for a heavily-overdrawn scene can be 5-20x: an early-Z pass on a typical opaque deferred prepass at 1080p discards 80-95% of fragments before they cost anything; the same shader running with late-Z pays the full ALU and bandwidth cost for every fragment.

## What the rule fires on

Pixel shaders that contain `discard` (or its alias `clip(...)` for negative arguments) reachable from a non-uniform branch, when the entry point is not annotated with `[earlydepthstencil]`. The rule also flags shaders that write `SV_Depth` from any non-uniform control-flow path, since this likewise inhibits early-Z. The pattern is the silent, default-on case where a single late `discard` causes the driver to demote the entire shader to late-Z and pay the full cost of shading every covered fragment regardless of its visibility.

See the [What it detects](../rules/early-z-disabled-by-conditional-discard.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/early-z-disabled-by-conditional-discard.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[early-z-disabled-by-conditional-discard.md -> Examples](../rules/early-z-disabled-by-conditional-discard.md#examples).

## See also

- [Rule page](../rules/early-z-disabled-by-conditional-discard.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
