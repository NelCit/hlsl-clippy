---
title: "mip-clamp-zero-on-mipped-texture"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: mip-clamp-zero-on-mipped-texture
---

# mip-clamp-zero-on-mipped-texture

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mip-clamp-zero-on-mipped-texture.md](../rules/mip-clamp-zero-on-mipped-texture.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mipmaps on every desktop GPU exist for two reasons: anti-aliasing minified samples (the higher-frequency texels of mip 0 alias under minification, so the hardware blends in coarser mips when the screen-space derivatives indicate minification) and bandwidth amortisation (a coarser mip is 1/4 the texel count and 1/4 the bandwidth). The texture sampler unit on AMD RDNA 2/3 (TMU), NVIDIA Turing/Ada (TEX/L1), and Intel Xe-HPG samples mips by computing a fractional LOD from the screen-space derivatives, then trilinearly blending two adjacent mips. When `MaxLOD = 0`, the hardware clamps the LOD selection to mip 0 regardless of derivatives â€” minified samples re-enter the aliasing regime and bandwidth scales with screen footprint instead of texel footprint.

## What the rule fires on

A `SamplerState` whose descriptor pins `MaxLOD = 0` (or a `Sample`/`SampleLevel` call with `clamp = 0` arg, or a `MinMipLevel = 0` clamp on a sampler-feedback path) bound against a `Texture2D` / `Texture2DArray` / `TextureCube` resource that reflection reports as carrying more than one mip level. The detector cross-references the sampler's `MaxLOD` field via reflection with the resource's mip count via reflection. It does not fire on textures that are genuinely single-mip (loading a single-level surface with `MaxLOD = 0` is correct and intended); it fires only when mips exist and the sampler clamps them off.

See the [What it detects](../rules/mip-clamp-zero-on-mipped-texture.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mip-clamp-zero-on-mipped-texture.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mip-clamp-zero-on-mipped-texture.md -> Examples](../rules/mip-clamp-zero-on-mipped-texture.md#examples).

## See also

- [Rule page](../rules/mip-clamp-zero-on-mipped-texture.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
