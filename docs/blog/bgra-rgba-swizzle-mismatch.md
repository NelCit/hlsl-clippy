---
title: "bgra-rgba-swizzle-mismatch"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: bgra-rgba-swizzle-mismatch
---

# bgra-rgba-swizzle-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/bgra-rgba-swizzle-mismatch.md](../rules/bgra-rgba-swizzle-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`DXGI_FORMAT_B8G8R8A8_UNORM` is the historical swap-chain format for D3D presentation; many older paths (especially UI / IMGUI overlays composited onto the swap-chain) still use BGRA8 throughout. The hardware texture sampler on AMD RDNA 2/3, NVIDIA Turing/Ada, and Intel Xe-HPG reads BGRA8 storage and presents it to the shader as a `float4` whose `.x` lane carries the *blue* channel and `.z` lane carries the *red* channel â€” the storage order, not the conceptual RGBA order. There is no hardware swizzle on the load path on D3D12; the format-to-shader-view mapping is exactly the storage layout. (Vulkan exposes a per-view `componentMapping` swizzle that can compensate at descriptor creation; D3D12 does not.)

## What the rule fires on

A shader that reads `.rgba` (or any subset like `.r`, `.rgb`, `.gb`) from a `Texture2D<float4>` (or analogous typed view) whose underlying resource format, as reported by Slang reflection, is `DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_FORMAT_B8G8R8A8_UNORM_SRGB`, or `DXGI_FORMAT_B8G8R8X8_UNORM`, without a corresponding `.bgra` swizzle to compensate for the BGRA-to-RGBA channel order. The detector cross-references the sample call site's swizzle (or absence thereof) with the resource format from reflection. It does not fire on RGBA-format resources, nor on BGRA resources where the shader explicitly swizzles `.bgra` (or applies an equivalent component reorder).

See the [What it detects](../rules/bgra-rgba-swizzle-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/bgra-rgba-swizzle-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[bgra-rgba-swizzle-mismatch.md -> Examples](../rules/bgra-rgba-swizzle-mismatch.md#examples).

## See also

- [Rule page](../rules/bgra-rgba-swizzle-mismatch.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
