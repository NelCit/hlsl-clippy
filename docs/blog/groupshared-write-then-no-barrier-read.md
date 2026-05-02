---
title: "groupshared-write-then-no-barrier-read: Compute shader code paths where a thread writes a `groupshared` location and a different…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-write-then-no-barrier-read
---

# groupshared-write-then-no-barrier-read

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-write-then-no-barrier-read.md](../rules/groupshared-write-then-no-barrier-read.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The D3D12 compute model guarantees that within a thread group, all threads share LDS / groupshared memory, but it does NOT guarantee any ordering between threads' memory operations unless an explicit barrier is issued. On AMD RDNA 2/3, threads in a wave run in lock-step, but threads in different waves of the same group are scheduled independently and can be hundreds of cycles apart at any given instruction; without a barrier, a read from another wave's slot may return the previous frame's value, the LDS-uninitialised pattern (zero on AMD, undefined on NVIDIA), or a torn write halfway through a non-atomic store. On NVIDIA Turing / Ada, the same applies across warps in a thread block: the SM scheduler issues warps in arbitrary order and the L1/SHM coherence boundary is the explicit barrier instruction. On Intel Xe-HPG, the EU thread scheduler likewise serialises across barrier points only.

## What the rule fires on

Compute shader code paths where a thread writes a `groupshared` location and a different thread (or the same thread on a subsequent iteration of a loop with cross-iteration dependence) reads from the same array region without a `GroupMemoryBarrierWithGroupSync` or `AllMemoryBarrierWithGroupSync` between the write and the read. The rule analyses index expressions to determine when reads can target a slot another thread has written: writes indexed by `SV_GroupIndex` followed by reads indexed by anything other than the same `SV_GroupIndex` (a neighbour offset, a constant, a transposed coordinate) are the canonical hits.

See the [What it detects](../rules/groupshared-write-then-no-barrier-read.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-write-then-no-barrier-read.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-write-then-no-barrier-read.md -> Examples](../rules/groupshared-write-then-no-barrier-read.md#examples).

## See also

- [Rule page](../rules/groupshared-write-then-no-barrier-read.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
