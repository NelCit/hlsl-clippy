---
title: "groupshared-atomic-replaceable-by-wave: `InterlockedAdd(gs[k], expr)`, `InterlockedOr(gs[k], mask)`, `InterlockedAnd(gs[k], mask)`, `InterlockedXor(gs[k], mask)`, `InterlockedMin(gs[k], val)`, or `InterlockedMax(gs[k], val)` against…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-atomic-replaceable-by-wave
---

# groupshared-atomic-replaceable-by-wave

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-atomic-replaceable-by-wave.md](../rules/groupshared-atomic-replaceable-by-wave.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

LDS atomics on every modern GPU serialise on the cell address. When a wave of 32 lanes (or 64 on RDNA wave64) hits the same `InterlockedAdd(gs[0], val)` site, the LDS atomic unit processes them sequentially: 32 round trips through the atomic ALU on AMD RDNA 2/3, 32 through NVIDIA Ada's shared-memory atomic unit, similar on Intel Xe-HPG. Even with hardware coalescing of monotonic-add operations on some IHVs, the worst case is 32x the single-atomic latency, and the atomic unit is single-issue per cycle so it stalls every other LDS access in the same wave for the duration. A 64-lane wave on RDNA wave64 doubles the cost.

## What the rule fires on

`InterlockedAdd(gs[k], expr)`, `InterlockedOr(gs[k], mask)`, `InterlockedAnd(gs[k], mask)`, `InterlockedXor(gs[k], mask)`, `InterlockedMin(gs[k], val)`, or `InterlockedMax(gs[k], val)` against a *single* groupshared cell — typically a counter at index 0 — where every active lane in the wave contributes a value derivable via the corresponding `WaveActive*` reduction. The rule fires when one wave-reduce + a single representative-lane atomic would replace 32 (RDNA wave32 / NVIDIA / Xe-HPG) or 64 (RDNA wave64) per-lane LDS atomics with one. Distinct from [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md), which targets a small fixed set of bins; this rule targets accumulation into one cell.

See the [What it detects](../rules/groupshared-atomic-replaceable-by-wave.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-atomic-replaceable-by-wave.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-atomic-replaceable-by-wave.md -> Examples](../rules/groupshared-atomic-replaceable-by-wave.md#examples).

## See also

- [Rule page](../rules/groupshared-atomic-replaceable-by-wave.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
