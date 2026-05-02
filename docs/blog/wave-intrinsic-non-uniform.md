---
title: "wave-intrinsic-non-uniform"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: wave-intrinsic-non-uniform
---

# wave-intrinsic-non-uniform

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave-intrinsic-non-uniform.md](../rules/wave-intrinsic-non-uniform.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Wave intrinsics operate across the set of lanes that are currently active at the instruction's execution point. When all lanes in a wave enter the intrinsic together, the operation is well-defined: `WaveActiveSum(x)` sums the value `x` from every lane in the wave. But when the intrinsic executes inside a divergent branch, only the subset of lanes that took that branch are active â€” the rest are masked off by the hardware. The result of `WaveActiveSum` in this position is the sum across only the participating subset, not the full wave. This is almost never what the programmer intended; more importantly, the participating subset varies from wave to wave depending on data values, making the result non-deterministic across runs on the same input if the hardware scheduler changes wave composition. The D3D12 and Vulkan specifications classify this as undefined behaviour for operations that require wave uniformity at entry.

## What the rule fires on

Calls to any wave intrinsic that operates across the active lane set â€” `WaveActiveSum`, `WaveActiveProduct`, `WaveActiveMin`, `WaveActiveMax`, `WaveActiveBitAnd`, `WaveActiveBitOr`, `WaveActiveBitXor`, `WaveActiveAllTrue`, `WaveActiveAnyTrue`, `WaveActiveAllEqual`, `WaveActiveBallot`, `WavePrefixSum`, `WavePrefixProduct`, `WaveReadLaneFirst`, `WaveReadLaneAt`, `WaveMatch`, `WaveMultiPrefixSum`, and `WaveMultiPrefixProduct` â€” when they appear inside a branch whose condition is non-uniform across the threads of the wave. The rule fires when the predicate is derived from per-thread varying data (thread IDs, buffer reads with thread-varying index, per-pixel varying inputs) rather than from a provably uniform source (cbuffer fields, literal constants, `WaveIsFirstLane` results).

See the [What it detects](../rules/wave-intrinsic-non-uniform.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave-intrinsic-non-uniform.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave-intrinsic-non-uniform.md -> Examples](../rules/wave-intrinsic-non-uniform.md#examples).

## See also

- [Rule page](../rules/wave-intrinsic-non-uniform.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
