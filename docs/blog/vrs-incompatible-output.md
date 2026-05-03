---
title: "vrs-incompatible-output"
date: 2026-05-02
author: shader-clippy maintainers
category: vrs
tags: [hlsl, performance, vrs]
status: stub
related-rule: vrs-incompatible-output
---

# vrs-incompatible-output

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/vrs-incompatible-output.md](../rules/vrs-incompatible-output.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

VRS is a coarse-shading optimisation: NVIDIA Turing and Ada Lovelace expose Tier 1 (per-draw) and Tier 2 (image-based + per-primitive) shading rates that let one PS invocation cover up to a 4x4 pixel region. AMD RDNA 2/3 implements the same surface as Variable Rate Shading at the rasterizer, with hardware that broadcasts the single shaded result across the coarse footprint. Intel Xe-HPG (Arc/Battlemage) added VRS Tier 2 with the same semantics. The whole point is to amortise PS work across multiple raster samples â€” the wave executes one set of derivatives, one set of sample fetches, and one ALU sequence per coarse fragment.

## What the rule fires on

Pixel-shader entry points that write per-sample outputs (`SV_Coverage`, `SV_SampleIndex`, an `[earlydepthstencil]`-marked `SV_Depth`, or per-sample interpolated inputs marked `sample`) while a Variable Rate Shading (VRS) shading rate coarser than 1x1 is applied to the draw. The rule uses Slang reflection to enumerate the entry's output semantics and pixel-shader attributes, then flags combinations that the D3D12 VRS specification calls out as forcing the runtime to silently drop the shading-rate request and revert to per-pixel shading. Common offenders: a shader that writes both a coarse colour and `SV_Coverage`, or a deferred g-buffer pass that uses VRS Tier 2 image-based shading rates while still emitting MSAA-aware per-sample inputs.

See the [What it detects](../rules/vrs-incompatible-output.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/vrs-incompatible-output.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[vrs-incompatible-output.md -> Examples](../rules/vrs-incompatible-output.md#examples).

## See also

- [Rule page](../rules/vrs-incompatible-output.md) -- canonical reference + change log.
- [vrs overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
