---
title: "sv-depth-vs-conservative-depth: A pixel-shader entry that writes `SV_Depth` (a per-pixel depth override) without using one of…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: vrs
tags: [hlsl, performance, vrs]
status: stub
related-rule: sv-depth-vs-conservative-depth
---

# sv-depth-vs-conservative-depth

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sv-depth-vs-conservative-depth.md](../rules/sv-depth-vs-conservative-depth.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Early depth-stencil (early-Z, early-S) is a hidden-surface optimisation that runs the depth/stencil test *before* the pixel shader executes. On NVIDIA Turing/Ada Lovelace and AMD RDNA 2/3, the rasterizer hands eligible fragments to the PixelHash unit, and the GPU uses the rasterised depth to cull occluded pixels before allocating a wave. Intel Xe-HPG (Arc Alchemist, Battlemage) implements the same early-Z stage. When `SV_Depth` is written from PS without any ordering hint, the hardware cannot use the rasterised depth for the test — the actual depth might be anywhere — so it disables early-Z and runs the full PS to compute the depth, *then* tests. For a draw that would have been 50% occluded, this doubles the PS workload.

## What the rule fires on

A pixel-shader entry that writes `SV_Depth` (a per-pixel depth override) without using one of the conservative-depth variants `SV_DepthGreaterEqual` or `SV_DepthLessEqual` when the value being written is monotonically related to the rasterised depth. The rule uses Slang reflection to identify the depth-output semantic on the PS entry, then walks the assignment expression: writes of the form `depth + bias`, `depth - bias`, `max(depth, k)`, or `min(depth, k)` are obvious conservative-direction patterns. Plain `SV_Depth` defeats early depth-stencil rejection on every IHV; the conservative variants preserve it under the matching ordering.

See the [What it detects](../rules/sv-depth-vs-conservative-depth.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sv-depth-vs-conservative-depth.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sv-depth-vs-conservative-depth.md -> Examples](../rules/sv-depth-vs-conservative-depth.md#examples).

## See also

- [Rule page](../rules/sv-depth-vs-conservative-depth.md) -- canonical reference + change log.
- [vrs overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
