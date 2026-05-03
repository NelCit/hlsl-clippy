---
title: "ser-coherence-hint-bits-overflow"
date: 2026-05-02
author: shader-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: ser-coherence-hint-bits-overflow
---

# ser-coherence-hint-bits-overflow

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/ser-coherence-hint-bits-overflow.md](../rules/ser-coherence-hint-bits-overflow.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Values above the cap are silently truncated by the SER scheduler, producing incoherent reorder. The SER scheduler buckets lanes by the masked low `hintBits` bits of the hint argument; an overflow widens the input but the hardware ignores the high bits, defeating the developer's grouping intent.

## What the rule fires on

A `MaybeReorderThread(hint, bits)` call where `bits > 16` (or the `HitObject::MaybeReorderThread` variant where the bits arg > 8). HLSL Specs proposal 0027 (SER, Accepted) caps the coherence-hint-bits arg.

See the [What it detects](../rules/ser-coherence-hint-bits-overflow.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/ser-coherence-hint-bits-overflow.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[ser-coherence-hint-bits-overflow.md -> Examples](../rules/ser-coherence-hint-bits-overflow.md#examples).

## See also

- [Rule page](../rules/ser-coherence-hint-bits-overflow.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
