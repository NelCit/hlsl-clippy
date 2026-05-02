---
title: "manual-wave-reduction-pattern"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: manual-wave-reduction-pattern
---

# manual-wave-reduction-pattern

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-wave-reduction-pattern.md](../rules/manual-wave-reduction-pattern.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Every modern GPU implements wave-level reductions as a primitive on dedicated cross-lane hardware: AMD RDNA 2/3 uses DPP (Data-Parallel Primitives) and SDWA paths through the SIMD unit, completing a 32-lane reduction in 5 cycles (logâ‚‚ 32) on RDNA 3; NVIDIA Ada Lovelace exposes `__shfl_xor_sync` / shfl-tree wired into the warp shuffle network, similarly 5 cycles for a 32-lane warp; Intel Xe-HPG provides equivalent subgroup-reduce intrinsics through the vector ALU's lane-crossing path. The HLSL `WaveActive*` family compiles to those primitives directly. A hand-rolled equivalent â€” whether it goes through LDS atomics, a tree-reduction with barriers, or an explicit `WaveReadLaneAt` ladder â€” replaces those 5 dedicated cycles with 32-64 ALU operations plus LDS / barrier traffic.

## What the rule fires on

Hand-rolled reductions that reproduce the semantics of `WaveActiveSum`, `WaveActiveProduct`, `WaveActiveMin`, `WaveActiveMax`, `WaveActiveBitOr`, `WaveActiveBitAnd`, `WaveActiveBitXor`, or `WaveActiveCountBits`. Pattern shapes detected: (a) a `for` loop that accumulates per-lane values into a groupshared cell via `InterlockedAdd` (or the corresponding atomic), (b) a tree-reduction loop over a groupshared array with halving stride and barriers between rounds, and (c) a same-wave shuffle-tree implemented via `WaveReadLaneAt(x, i ^ k)` for `k = 1, 2, 4, 8, 16` ladder. All three shapes are subsumed by a single `WaveActive*` call when the reduction scope is the wave (the same-wave shuffle tree case) or by `WaveActive*` followed by an LDS atomic / barrier when the scope is the workgroup.

See the [What it detects](../rules/manual-wave-reduction-pattern.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-wave-reduction-pattern.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-wave-reduction-pattern.md -> Examples](../rules/manual-wave-reduction-pattern.md#examples).

## See also

- [Rule page](../rules/manual-wave-reduction-pattern.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
