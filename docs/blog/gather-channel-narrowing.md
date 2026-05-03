---
title: "gather-channel-narrowing"
date: 2026-05-02
author: shader-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: gather-channel-narrowing
---

# gather-channel-narrowing

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/gather-channel-narrowing.md](../rules/gather-channel-narrowing.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`Gather` is a TMU operation that fetches the four texels in the 2x2 bilinear footprint surrounding a UV coordinate and returns one scalar channel from each texel packed into a `float4`. On all current GPU architectures (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG), a single `Gather` instruction completes in one TMU issue cycle regardless of which channel it returns. The hardware has dedicated `GatherRed`, `GatherGreen`, `GatherBlue`, and `GatherAlpha` instruction variants that select the channel at the TMU instruction level, not in a post-processing step.

## What the rule fires on

Expressions of the form `texture.Gather(sampler, uv).r`, `.g`, `.b`, or `.a` â€” where `Gather` is called and only a single scalar channel of the resulting `float4` is consumed. The rule fires when the swizzle immediately follows the `Gather` call and the remaining three components are provably dead (not stored, not passed to a function, not part of a larger swizzle). The equivalent hardware-direct call is `GatherRed`, `GatherGreen`, `GatherBlue`, or `GatherAlpha` respectively. The rule fires on `Texture2D`, `Texture2DArray`, and `TextureCube` gather variants. It does not fire when more than one channel of the gathered `float4` is used downstream.

See the [What it detects](../rules/gather-channel-narrowing.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/gather-channel-narrowing.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[gather-channel-narrowing.md -> Examples](../rules/gather-channel-narrowing.md#examples).

## See also

- [Rule page](../rules/gather-channel-narrowing.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
