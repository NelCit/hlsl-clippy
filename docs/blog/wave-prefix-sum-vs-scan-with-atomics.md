---
title: "wave-prefix-sum-vs-scan-with-atomics"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: wave-prefix-sum-vs-scan-with-atomics
---

# wave-prefix-sum-vs-scan-with-atomics

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave-prefix-sum-vs-scan-with-atomics.md](../rules/wave-prefix-sum-vs-scan-with-atomics.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Prefix-sum scan is the second-most-common cross-lane primitive in compute shaders (after `WaveActiveSum`). Every modern GPU implements it as a dedicated cross-lane primitive: AMD RDNA 2/3 issues `WavePrefixSum` through DPP (Data-Parallel Primitives) in `logâ‚‚(wave_size)` cycles â€” 5 cycles on a wave32 RDNA 3, 6 on a wave64 RDNA 2; NVIDIA Ada Lovelace and Turing expose the equivalent through the warp-shfl prefix network, also 5 cycles per warp; Intel Xe-HPG's subgroup prefix-scan completes in `logâ‚‚(subgroup_size)` cycles on the cross-lane unit. The HLSL `WavePrefixSum` intrinsic compiles to those primitives directly.

## What the rule fires on

A hand-rolled prefix-sum (exclusive or inclusive scan) implemented as a multi-pass groupshared-plus-barrier sequence. Pattern shapes detected: (a) a Hillisâ€“Steele up-sweep of the form `for (uint stride = 1; stride < N; stride <<= 1) { if (gi >= stride) g_Scan[gi] += g_Scan[gi - stride]; GroupMemoryBarrierWithGroupSync(); }`, (b) a Blelloch up-sweep / down-sweep with the equivalent barrier ladder, and (c) any scan implemented as a sequence of `InterlockedAdd` against a running counter where lanes consume monotone slot indices. All three patterns can be replaced by `WavePrefixSum` (within a wave) plus at most one barrier-and-broadcast step (across waves in a workgroup).

See the [What it detects](../rules/wave-prefix-sum-vs-scan-with-atomics.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave-prefix-sum-vs-scan-with-atomics.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave-prefix-sum-vs-scan-with-atomics.md -> Examples](../rules/wave-prefix-sum-vs-scan-with-atomics.md#examples).

## See also

- [Rule page](../rules/wave-prefix-sum-vs-scan-with-atomics.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
