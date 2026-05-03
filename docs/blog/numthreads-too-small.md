---
title: "numthreads-too-small"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: numthreads-too-small
---

# numthreads-too-small

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/numthreads-too-small.md](../rules/numthreads-too-small.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A thread group with fewer than 32 threads can never fill a single wave. On RDNA 3 with a 32-wide wave, a thread group of 16 threads is launched as one wave with 16 active lanes and 16 permanently masked-off lanes. Those 16 masked lanes are not recycled by the hardware â€” the wave occupies a full wave slot in the CU's wave scheduler for the entire duration of the kernel, including all memory latency hiding. This means that at best 50% of the execution resources in that wave slot are doing productive work. Across a full dispatch of many thread groups, effective VALU throughput is capped at 50% of the achievable rate for that occupancy level.

## What the rule fires on

A compute or amplification shader whose `[numthreads(X, Y, Z)]` attribute produces a total thread count (X * Y * Z) strictly less than the minimum hardware wave size of 32. Values such as `[numthreads(4, 4, 1)]` (16 threads), `[numthreads(1, 1, 1)]` (1 thread), and `[numthreads(8, 1, 1)]` (8 threads) all fire the rule. The threshold is fixed at 32 regardless of the `target-wave-size` option from `numthreads-not-wave-aligned`, because 32 is the minimum wave size on all currently targeted hardware families (AMD RDNA/RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace). The rule does not fire when the total thread count is exactly 32 or any positive multiple of 32.

See the [What it detects](../rules/numthreads-too-small.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/numthreads-too-small.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[numthreads-too-small.md -> Examples](../rules/numthreads-too-small.md#examples).

## See also

- [Rule page](../rules/numthreads-too-small.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
