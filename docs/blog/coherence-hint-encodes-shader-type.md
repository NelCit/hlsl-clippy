---
title: "coherence-hint-encodes-shader-type"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: coherence-hint-encodes-shader-type
---

# coherence-hint-encodes-shader-type

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coherence-hint-encodes-shader-type.md](../rules/coherence-hint-encodes-shader-type.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The whole point of SER is that the driver's scheduler already knows how to coalesce lanes by their downstream shader (hit group / miss / etc.). NVIDIA Ada Lovelace's scheduler uses the HitObject's intrinsic data to pre-bucket lanes; AMD RDNA 4 / Vulkan SER extensions match. The `coherenceHint` argument is meant for *application-specific* axes the scheduler doesn't know about: which material is hit, which BVH instance was traversed, which payload bucket the lane belongs to. Encoding the shader-table index into the hint duplicates information the scheduler already has and forces it to factor the same axis twice into its bucketing â€” at best wasted work, at worst a worse final grouping than the no-hint baseline.

## What the rule fires on

A `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose `coherenceHint` expression is data-flow-tainted by `hit.IsHit()` or `hit.GetShaderTableIndex()`. The SER scheduler already groups lanes by hit-group / miss-vs-hit; encoding the same information in the user-supplied coherence hint duplicates work and can confuse the scheduler's bucketing heuristic. The Phase 4 taint analysis tracks `IsHit` and `GetShaderTableIndex` returns through arithmetic and conditional expressions.

See the [What it detects](../rules/coherence-hint-encodes-shader-type.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coherence-hint-encodes-shader-type.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coherence-hint-encodes-shader-type.md -> Examples](../rules/coherence-hint-encodes-shader-type.md#examples).

## See also

- [Rule page](../rules/coherence-hint-encodes-shader-type.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
