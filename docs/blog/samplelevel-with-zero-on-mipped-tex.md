---
title: "samplelevel-with-zero-on-mipped-tex: Calls to `SampleLevel(sampler, uv, 0)` — or `SampleLevel(sampler, uv, 0.0)` — where the third…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: samplelevel-with-zero-on-mipped-tex
---

# samplelevel-with-zero-on-mipped-tex

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/samplelevel-with-zero-on-mipped-tex.md](../rules/samplelevel-with-zero-on-mipped-tex.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mip chains exist for two reasons: reducing aliasing in minification, and enabling the texture cache to fetch smaller footprints from VRAM when the on-screen footprint is small. When a mipped resource is sampled exclusively at mip 0, every fetch hits the highest-resolution level regardless of the rasterised pixel footprint. This defeats both goals: cache lines are wasted on high-resolution texels that are averaged away by the hardware's aniso filter, and the lower mip levels occupy VRAM bandwidth during allocation and streaming for no return. A 2048x2048 RGBA8 texture carries 4 MB at mip 0 alone; pinning all samples to mip 0 means the additional 1.3 MB of mip chain is uploaded and retained in VRAM but never touched.

## What the rule fires on

Calls to `SampleLevel(sampler, uv, 0)` — or `SampleLevel(sampler, uv, 0.0)` — where the third argument (the LOD parameter) is the literal zero, and where reflection data shows that the bound resource was declared with mip levels (a full mip chain or a partial chain with more than one level). The rule fires when a `Texture2D`, `TextureCube`, `Texture2DArray`, or similar mipped resource type is paired with an explicit mip-0 lock that is not guarded by a compile-time constant or a `[mips(1)]` resource annotation. It does not fire on resources explicitly declared as single-mip (`Texture2D<float4> T : register(t0); // mips 1`), on `Buffer<>` or `RWTexture2D<>` objects, or when the lod argument is a non-zero expression.

See the [What it detects](../rules/samplelevel-with-zero-on-mipped-tex.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/samplelevel-with-zero-on-mipped-tex.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[samplelevel-with-zero-on-mipped-tex.md -> Examples](../rules/samplelevel-with-zero-on-mipped-tex.md#examples).

## See also

- [Rule page](../rules/samplelevel-with-zero-on-mipped-tex.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
