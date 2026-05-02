---
title: "manual-srgb-conversion"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: manual-srgb-conversion
---

# manual-srgb-conversion

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-srgb-conversion.md](../rules/manual-srgb-conversion.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`*_SRGB` texture formats on every modern GPU IHV invoke a hardware sRGB-to-linear converter on every texel fetch. AMD RDNA 2/3 documents the converter as part of the TMU's format-decode pipeline; NVIDIA Turing/Ada and Intel Xe-HPG provide equivalent hardware. The hardware converter runs for free on the sample path â€” there is no shader-cycle cost â€” and it implements the exact piecewise sRGB curve from the IEC 61966-2-1 specification, with sub-bit precision better than what a 32-bit `pow(c, 2.2)` can achieve given the precision loss of the transcendental.

## What the rule fires on

A hand-rolled gamma 2.2 or sRGB transfer function (`pow(c, 2.2)`, `pow(c.rgb, 2.2)`, `pow(c, 1.0/2.2)`, the canonical piecewise sRGB inverse, or the constant-folded expansion thereof) applied to the result of a `Sample`/`Load` call against a resource whose format reflection reports as one of `DXGI_FORMAT_*_SRGB` (e.g. `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`, `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB`, `DXGI_FORMAT_BC1_UNORM_SRGB` ... `BC7_UNORM_SRGB`). The detector pattern-matches the gamma-curve expression on the AST side and cross-references the sampled resource's format on the reflection side. It does not fire on samples from non-sRGB resources (where the manual conversion may be the correct conversion).

See the [What it detects](../rules/manual-srgb-conversion.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-srgb-conversion.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-srgb-conversion.md -> Examples](../rules/manual-srgb-conversion.md#examples).

## See also

- [Rule page](../rules/manual-srgb-conversion.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
