---
title: "interlocked-bin-without-wave-prereduce"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: interlocked-bin-without-wave-prereduce
---

# interlocked-bin-without-wave-prereduce

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/interlocked-bin-without-wave-prereduce.md](../rules/interlocked-bin-without-wave-prereduce.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`InterlockedAdd` to LDS / groupshared memory issues a `ds_add_u32` (AMD RDNA) or `ATOMS.ADD` (NVIDIA) instruction per active lane, but the hardware serialises atomic ops that target the same address. On AMD RDNA 2/3, LDS atomics to one bank-aligned address are issued at one-per-clock from the LDS arbiter; if 32 lanes target one bin, the operation takes 32 clocks rather than the single clock a non-conflicting batch would take. NVIDIA Turing and Ada serialise warp-wide shared-memory atomics through the Locked Atomic Throughput pipe, with similar one-per-clock per-bank serialisation. Intel Xe-HPG's SLM atomics serialise through the L1$ bank arbiter at comparable rates. UAV atomics are even worse because they cross the L2 / memory hierarchy.

## What the rule fires on

Calls to `InterlockedAdd`, `InterlockedOr`, or `InterlockedXor` against a `groupshared` array or a UAV buffer with a small fixed bin count (â‰¤ 32 indices) where the index is computed per-thread but the rule can prove the bin set is small enough that wave-level pre-reduction with `WaveActiveSum` or `WavePrefixSum` would coalesce most of the contention. Histograms with up to 32 bins, summary counters, per-material lane counters, and per-cluster occupancy maps are the canonical examples.

See the [What it detects](../rules/interlocked-bin-without-wave-prereduce.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/interlocked-bin-without-wave-prereduce.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[interlocked-bin-without-wave-prereduce.md -> Examples](../rules/interlocked-bin-without-wave-prereduce.md#examples).

## See also

- [Rule page](../rules/interlocked-bin-without-wave-prereduce.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
