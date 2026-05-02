---
title: "wave-intrinsic-helper-lane-hazard: Pixel shaders that call any cross-lane wave intrinsic (`WaveActiveSum`, `WaveActiveMin`, `WavePrefixSum`, `WaveActiveBallot`, `WaveReadLaneAt`, etc.)…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: wave-intrinsic-helper-lane-hazard
---

# wave-intrinsic-helper-lane-hazard

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave-intrinsic-helper-lane-hazard.md](../rules/wave-intrinsic-helper-lane-hazard.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The pixel-shader execution model on every modern GPU keeps the four lanes of a 2x2 quad co-resident even when some lanes are not part of a covered triangle. The non-covered lanes are called helper lanes; their results are discarded at the end, but they execute every instruction so that derivatives (`ddx`, `ddy`, implicit-LOD `Sample`) can read consistent coordinate values from all four lanes. This is essential for correct mip selection and gradient-based shading. After a `discard`, the affected lane likewise becomes a helper: it continues executing for derivative consistency but its colour and depth output are dropped.

## What the rule fires on

Pixel shaders that call any cross-lane wave intrinsic (`WaveActiveSum`, `WaveActiveMin`, `WavePrefixSum`, `WaveActiveBallot`, `WaveReadLaneAt`, etc.) on a code path that is reachable after a `discard` or `clip` may have executed, when the entry point does not opt out of helper-lane participation via SM 6.6 `IsHelperLane()` guards. The same hazard applies to wave intrinsics placed inside loops where some quad lanes have early-exited: the inactive lanes are still tracked as helper lanes and may participate in cross-lane operations, contributing values that the algorithm did not intend to include.

See the [What it detects](../rules/wave-intrinsic-helper-lane-hazard.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave-intrinsic-helper-lane-hazard.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave-intrinsic-helper-lane-hazard.md -> Examples](../rules/wave-intrinsic-helper-lane-hazard.md#examples).

## See also

- [Rule page](../rules/wave-intrinsic-helper-lane-hazard.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
