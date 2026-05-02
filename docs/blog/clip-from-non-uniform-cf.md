---
title: "clip-from-non-uniform-cf"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: clip-from-non-uniform-cf
---

# clip-from-non-uniform-cf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/clip-from-non-uniform-cf.md](../rules/clip-from-non-uniform-cf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Pixel-shader early-Z (and stencil) testing is a critical bandwidth optimisation: the rasteriser tests depth before invoking the pixel shader, so occluded fragments never run. A pixel shader that may issue `clip(x)` (or `discard`) modifies the depth attachment as a side effect of the shader body, which forces the hardware to defer depth update until after shader execution. On AMD RDNA 2/3, the GE / SX hardware downgrades from "early-Z + early-stencil" to "late-Z" for the entire pipeline state when the shader contains a clip / discard reachable on any path â€” losing the bandwidth savings on every fragment, not just the clipped ones. NVIDIA Ada applies the same downgrade per-pipeline-state; Intel Xe-HPG behaves analogously. The shader author can opt back into early-Z by adding the `[earlydepthstencil]` attribute, which promises the hardware that the depth value is unaffected by the shader (the clip / discard then retires the lane *after* the early-Z test has already updated depth, accepting the resulting incorrect depth in exchange for the bandwidth recovery â€” a trade-off only the author can authorise).

## What the rule fires on

A call to `clip(x)` (the HLSL component-wise discard intrinsic that retires the lane when any component of `x` is negative) inside a pixel-shader entry point whose containing function lacks the `[earlydepthstencil]` attribute and is reachable from non-uniform control flow â€” that is, the path from entry to the `clip` call passes through at least one branch whose condition is per-lane varying. Distinct from the locked [early-z-disabled-by-conditional-discard](early-z-disabled-by-conditional-discard.md), which fires on `discard`; `clip(x)` has its own semantics (retire on negative component, threshold per channel) and an independent suppression scope.

See the [What it detects](../rules/clip-from-non-uniform-cf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/clip-from-non-uniform-cf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[clip-from-non-uniform-cf.md -> Examples](../rules/clip-from-non-uniform-cf.md#examples).

## See also

- [Rule page](../rules/clip-from-non-uniform-cf.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
