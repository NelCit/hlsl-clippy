---
title: "nointerpolation-mismatch"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: nointerpolation-mismatch
---

# nointerpolation-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/nointerpolation-mismatch.md](../rules/nointerpolation-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The rasteriser's default interpolation mode is barycentric: for each pixel sample, the hardware computes `attr_pixel = w0*attr_v0 + w1*attr_v1 + w2*attr_v2`, where `w0..w2` are the perspective-correct barycentric weights. On AMD RDNA 2 and RDNA 3, this is performed by the parameter cache and the per-pixel `v_interp_*` instructions, each of which consumes a VALU slot. On NVIDIA Turing and Ada Lovelace, the SM's attribute interpolator unit issues an `IPA` instruction per attribute component per pixel â€” Turing's whitepaper documents `IPA` as a half-rate operation against general FP32 throughput, and Ada inherits the same scheduling. For a 4-component attribute that is then immediately cast to `int` and used as an index, every one of those interpolator cycles is wasted: the fractional output is discarded the instant the cast happens.

## What the rule fires on

A vertex-output / pixel-input attribute that the pixel shader uses in a flat-only context â€” read as an integer (`asint`, `(uint)`), used as an array index, used as a `switch` selector, used as a buffer/texture index, or compared with `==` against a per-primitive integer constant â€” but whose declaration on the vertex-output side does not carry the `nointerpolation` modifier. The rule cross-references the matched VS-output struct member (or geometry/mesh-shader output) with the PS-input usage and fires on the VS-side declaration, suggesting that `nointerpolation` be added so the rasteriser broadcasts the provoking-vertex value unchanged instead of computing a barycentric-weighted blend.

See the [What it detects](../rules/nointerpolation-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/nointerpolation-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[nointerpolation-mismatch.md -> Examples](../rules/nointerpolation-mismatch.md#examples).

## See also

- [Rule page](../rules/nointerpolation-mismatch.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
