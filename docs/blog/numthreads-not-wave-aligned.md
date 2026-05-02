---
title: "numthreads-not-wave-aligned"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: numthreads-not-wave-aligned
---

# numthreads-not-wave-aligned

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/numthreads-not-wave-aligned.md](../rules/numthreads-not-wave-aligned.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The wave (or warp, in NVIDIA terminology) is the atomic unit of GPU execution. A wave is a group of lanes that share a single instruction stream: all lanes issue the same instruction on the same clock cycle, with divergent lanes masked off. On NVIDIA Turing and Ada Lovelace, the hardware wave size is 32 (a warp of 32 threads). On AMD RDNA and RDNA 2/3, the hardware supports both 32-wide and 64-wide waves; the driver defaults to 32-wide for SM6.x compute unless the shader explicitly requests 64-wide via `[WaveSize(64)]`. On Intel Xe-HPG, the hardware SIMD width is 8, 16, or 32 lanes; the compiler chooses based on register pressure.

## What the rule fires on

A compute or amplification shader whose `[numthreads(X, Y, Z)]` attribute produces a total thread count (X * Y * Z) that is not a multiple of the configured wave size. The default `target-wave-size` is 32 (matching NVIDIA Turing/Ada and AMD RDNA/RDNA 2/RDNA 3 in their default 32-wide mode). A total that is not divisible by 32 means the final wave of every thread group is launched with some lanes masked off â€” hardware dead weight. The rule fires on `[numthreads(7, 7, 1)]` (49 threads, not a multiple of 32 or 64), `[numthreads(5, 13, 1)]` (65 threads), and similar configurations. It does not fire when the total is an exact multiple of the configured wave size.

See the [What it detects](../rules/numthreads-not-wave-aligned.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/numthreads-not-wave-aligned.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[numthreads-not-wave-aligned.md -> Examples](../rules/numthreads-not-wave-aligned.md#examples).

## See also

- [Rule page](../rules/numthreads-not-wave-aligned.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
