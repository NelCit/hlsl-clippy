---
title: "texture-as-buffer: A `Texture2D`, `Texture1D`, or `Texture2DArray` resource that is accessed exclusively through integer-coordinate `Load(int3(x, 0,…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: texture-as-buffer
---

# texture-as-buffer

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/texture-as-buffer.md](../rules/texture-as-buffer.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On all current GPU families (AMD RDNA / RDNA 2 / RDNA 3, NVIDIA Turing / Ada Lovelace, Intel Xe-HPG), a `Texture2D` resource carries metadata overhead that a `Buffer<>` does not. The hardware must maintain a surface descriptor that includes width, height, depth, mip count, sample count, tile mode, and format swizzle. Issuing a texel-fetch instruction (`image_load` on GCN/RDNA, `ld` on DXIL-SM6) to a 2D texture object instructs the TMU or the L1 texture cache to resolve a 2D address through the surface descriptor pipeline even when only one dimension varies. On RDNA 3, the image-fetch path has a fixed per-instruction overhead for descriptor decode that a raw-buffer load (`buffer_load_dword`) avoids entirely. The raw-buffer path also benefits from the scalar unit (SMEM / `s_load`) when the index is wave-uniform, reducing register pressure.

## What the rule fires on

A `Texture2D`, `Texture1D`, or `Texture2DArray` resource that is accessed exclusively through integer-coordinate `Load(int3(x, 0, 0), 0)` or `Load(int3(x, 0, 0))` calls, where the y and z coordinates are always the compile-time literal zero and the mip level is always zero. When reflection confirms that the resource is bound as a two-dimensional object but the shader never uses the second (or third) dimension, and never uses a sampler, the resource is acting as a flat linear array. The rule suggests replacing it with `Buffer<T>` for read-only access or `StructuredBuffer<T>` if the element type is a user-defined struct. It does not fire when any access uses a non-zero y coordinate, a non-zero mip level, a sampler, or `Gather` variants.

See the [What it detects](../rules/texture-as-buffer.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/texture-as-buffer.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[texture-as-buffer.md -> Examples](../rules/texture-as-buffer.md#examples).

## See also

- [Rule page](../rules/texture-as-buffer.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
