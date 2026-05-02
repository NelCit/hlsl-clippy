---
title: "wavesize-32-on-xe2-misses-simd16"
date: 2026-05-02
author: hlsl-clippy maintainers
category: xe2
tags: [hlsl, performance, xe2]
status: stub
related-rule: wavesize-32-on-xe2-misses-simd16
---

# wavesize-32-on-xe2-misses-simd16

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavesize-32-on-xe2-misses-simd16.md](../rules/wavesize-32-on-xe2-misses-simd16.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Intel Xe2 / Battlemage SIMD16 native execution saves one address-gen cycle per dispatch over SIMD32. Per Chips and Cheese's Battlemage architecture deep-dive, kernels pinned to SIMD32 hide native efficiency on Xe2 -- the hardware can issue SIMD16 in the same throughput tier as SIMD32 but with lower address-generation latency.

## What the rule fires on

A compute / mesh / amplification kernel pinned `[WaveSize(32)]` under the `[experimental.target = xe2]` config gate.

See the [What it detects](../rules/wavesize-32-on-xe2-misses-simd16.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavesize-32-on-xe2-misses-simd16.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavesize-32-on-xe2-misses-simd16.md -> Examples](../rules/wavesize-32-on-xe2-misses-simd16.md#examples).

## See also

- [Rule page](../rules/wavesize-32-on-xe2-misses-simd16.md) -- canonical reference + change log.
- [xe2 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*

**TODO:** category-overview missing for `xe2`; linked overview is the closest sibling.