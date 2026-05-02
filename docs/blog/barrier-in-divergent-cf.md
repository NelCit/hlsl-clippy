---
title: "barrier-in-divergent-cf: Calls to `GroupMemoryBarrierWithGroupSync`, `DeviceMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync`, and the variants without `WithGroupSync` (`GroupMemoryBarrier`, `DeviceMemoryBarrier`, `AllMemoryBarrier`) when…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: barrier-in-divergent-cf
---

# barrier-in-divergent-cf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/barrier-in-divergent-cf.md](../rules/barrier-in-divergent-cf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU compute shaders run as independent thread groups. Within a thread group, threads share a pool of groupshared (LDS) memory and are expected to coordinate via barriers. A `GroupMemoryBarrierWithGroupSync` tells the hardware: stall every thread in this group until all of them reach this instruction. The GPU implements this by counting how many threads have checked in; only when the count reaches the group size does execution resume. If some threads never reach the barrier — because they took a divergent branch — the counter never reaches the group size. The result is a GPU hang: the threads that reached the barrier wait indefinitely. On AMD GCN and RDNA architectures, this stalls the entire compute unit because the scheduler cannot retire the wavefront. On NVIDIA architectures, the warp-level implementation deadlocks similarly. Neither the DX12 runtime nor the driver can detect this class of hang at API level; it manifests as a device-removed error or a TDR reset in production.

## What the rule fires on

Calls to `GroupMemoryBarrierWithGroupSync`, `DeviceMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync`, and the variants without `WithGroupSync` (`GroupMemoryBarrier`, `DeviceMemoryBarrier`, `AllMemoryBarrier`) when they appear inside a branch whose condition depends on a non-uniform value. Non-uniform in this context means any condition that is not provably identical for all threads in the thread group simultaneously — including conditions derived from `SV_DispatchThreadID`, `SV_GroupIndex`, per-pixel varying data, or any value loaded from a non-constant buffer with a thread-varying index. The rule fires on any `if`, `else if`, `else`, `for`, `while`, or `switch` that contains a synchronising barrier intrinsic and whose predicate is not demonstrably thread-group-uniform.

See the [What it detects](../rules/barrier-in-divergent-cf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/barrier-in-divergent-cf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[barrier-in-divergent-cf.md -> Examples](../rules/barrier-in-divergent-cf.md#examples).

## See also

- [Rule page](../rules/barrier-in-divergent-cf.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
