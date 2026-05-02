---
title: "texture-array-known-slice-uniform"
date: 2026-05-02
author: hlsl-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: texture-array-known-slice-uniform
---

# texture-array-known-slice-uniform

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/texture-array-known-slice-uniform.md](../rules/texture-array-known-slice-uniform.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A `Texture2DArray` resource descriptor carries an extra dimension in its hardware surface state: the array layer count. On AMD RDNA, NVIDIA Turing, and Intel Xe-HPG, the TMU's address-generation unit must resolve a 3D coordinate `(x, y, layer)` rather than a 2D coordinate `(x, y)` for every fetch. When the layer index is a constant across the entire dispatch, every thread in every wave loads the same layer, and the array dimension adds no useful variation â€” the shader is accessing a single fixed 2D slice of the array. The TMU still processes the full 3D address, however, and the resource descriptor still reserves VRAM for all array layers even if only one is ever read.

## What the rule fires on

Calls to `Texture2DArray.Sample(sampler, float3(uv, K))` or `Texture2DArray.SampleLevel(sampler, float3(uv, K), lod)` where the slice coordinate `K` (the z component of the UV argument) is statically determinable as dynamically uniform for the entire draw call or dispatch â€” specifically, when `K` is a `cbuffer` field, a root constant, or a literal value. The rule uses Slang reflection to confirm that the slice source is a constant buffer member, and then checks whether the array resource carries more than one slice. It does not fire when `K` is a per-thread value such as a UAV index, a `SV_InstanceID`-derived expression, or a groupshared variable.

See the [What it detects](../rules/texture-array-known-slice-uniform.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/texture-array-known-slice-uniform.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[texture-array-known-slice-uniform.md -> Examples](../rules/texture-array-known-slice-uniform.md#examples).

## See also

- [Rule page](../rules/texture-array-known-slice-uniform.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
