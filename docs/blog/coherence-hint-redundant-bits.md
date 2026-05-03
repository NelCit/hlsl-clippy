---
title: "coherence-hint-redundant-bits"
date: 2026-05-02
author: shader-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: coherence-hint-redundant-bits
---

# coherence-hint-redundant-bits

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coherence-hint-redundant-bits.md](../rules/coherence-hint-redundant-bits.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`MaybeReorderThread`'s coherence hint is the developer-supplied bucketing key the runtime uses to coalesce divergent lanes. NVIDIA Ada Lovelace's SER scheduler uses up to 16 bits of hint by default; AMD RDNA 4's SER implementation (when shipped) and the Vulkan `VK_EXT_ray_tracing_invocation_reorder` extension expose the same surface. The driver applies the hint's `hintBits` mask to the `coherenceHint` value and uses the masked bits to bucket lanes â€” fewer bits means a coarser bucketing and a potentially less effective reorder.

## What the rule fires on

A `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose `hintBits` argument is larger than necessary to express the actual coherence-hint value, or whose `coherenceHint` value has bits set above bit `hintBits-1`. The Phase 4 bit-range analysis tracks the constant or affine bound on the hint expression and compares it with the declared bit count.

See the [What it detects](../rules/coherence-hint-redundant-bits.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coherence-hint-redundant-bits.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coherence-hint-redundant-bits.md -> Examples](../rules/coherence-hint-redundant-bits.md#examples).

## See also

- [Rule page](../rules/coherence-hint-redundant-bits.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
