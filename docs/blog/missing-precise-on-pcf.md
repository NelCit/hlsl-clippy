---
title: "missing-precise-on-pcf"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: missing-precise-on-pcf
---

# missing-precise-on-pcf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/missing-precise-on-pcf.md](../rules/missing-precise-on-pcf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

HLSL gives the compiler wide latitude to reorder, fuse, and re-associate floating-point arithmetic. `(a * b) + c` may be lowered as a fused multiply-add (FMA) or as a separate multiply followed by an add; `(a + b) + c` may be evaluated as `a + (b + c)`; a perspective divide `x / w` may be lowered as `x * rcp(w)` with a single-Newton-step `rcp` whose error envelope differs by one or two ULPs from a true IEEE divide. Each of these choices is, in isolation, well within the IEEE-754 tolerance the spec promises â€” but the choice differs between vendors and between driver versions. AMD's RDNA shader compiler aggressively forms FMAs and prefers `v_rcp_f32` over IEEE divide; NVIDIA's Turing/Ada compiler also forms FMAs but with different operand-ordering heuristics; Intel Xe-HPG's IGC has its own pass ordering. The result: the same HLSL receiver-depth expression produces depths that disagree at the last 1-2 ULPs across IHVs.

## What the rule fires on

Depth-compare arithmetic in a pixel/compute shader â€” typically the receiver-side computation that feeds a `SampleCmp`, `SampleCmpLevelZero`, or `GatherCmp` call against a shadow-map texture, or a manual PCF (percentage-closer filtering) kernel that compares a computed receiver depth against several sampled shadow-map depths and accumulates the comparisons into a softness factor. The rule fires on the receiver-depth expression and the per-tap comparison expressions when none of the contributing locals are marked `precise`, when the shadow projection involves a perspective divide (`coord.xyz / coord.w`) or a depth-bias term added to the receiver depth, and when the result feeds a comparison sampler or an `if (recv < occluder)` test. The diagnostic suggests adding `precise` to the receiver-depth local and to any intermediate that participates in the perspective divide or the bias add.

See the [What it detects](../rules/missing-precise-on-pcf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/missing-precise-on-pcf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[missing-precise-on-pcf.md -> Examples](../rules/missing-precise-on-pcf.md#examples).

## See also

- [Rule page](../rules/missing-precise-on-pcf.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
