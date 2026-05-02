---
title: "redundant-texture-sample"
date: 2026-05-02
author: hlsl-clippy maintainers
category: memory
tags: [hlsl, performance, memory]
status: stub
related-rule: redundant-texture-sample
---

# redundant-texture-sample

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-texture-sample.md](../rules/redundant-texture-sample.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A texture sample is not free: the hardware must compute the MIP-map level, look up the texel footprint, fetch the relevant cache lines from L1 (the texture L0 cache on RDNA, or the TEX cache on Turing), interpolate the samples, and return a result to the ALU pipeline. On RDNA 3, a cache-hit sample from L0 still costs roughly 22 cycles of latency; an L1 miss escalates to ~100 cycles and an L2 miss to ~200-300 cycles. A redundant sample that could have been eliminated at compile time pays this latency again unnecessarily, and occupies a TMU slot that could have served a different wave's pending request.

## What the rule fires on

Two or more calls to the same texture's `Sample`, `SampleLevel`, or `SampleGrad` method with identical arguments â€” the same texture object, the same sampler, and the same UV coordinates â€” appearing in the same basic block with no intervening write to the texture or to the UV value. The compiler's built-in common-subexpression elimination (CSE) pass normally handles this, but it may fail when the duplicate calls are separated by a function call boundary, when the texture object is passed as a parameter (obscuring aliasing information), or when the compiler's alias analysis conservatively assumes the function may write the texture. The rule fires on the second (and subsequent) sample of any (texture, sampler, UV) triple that has already been sampled in the same basic block.

See the [What it detects](../rules/redundant-texture-sample.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-texture-sample.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-texture-sample.md -> Examples](../rules/redundant-texture-sample.md#examples).

## See also

- [Rule page](../rules/redundant-texture-sample.md) -- canonical reference + change log.
- [memory overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
