---
title: "wavereadlaneat-constant-zero-to-readfirst"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: wavereadlaneat-constant-zero-to-readfirst
---

# wavereadlaneat-constant-zero-to-readfirst

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavereadlaneat-constant-zero-to-readfirst.md](../rules/wavereadlaneat-constant-zero-to-readfirst.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`WaveReadLaneAt(x, lane)` is the general lane-index broadcast: every active lane in the wave receives the value of `x` from the lane numbered `lane`. The hardware contract is general-purpose â€” `lane` may be any value in `[0, WaveGetLaneCount())`, may differ across waves, and is required to be uniform across the active lanes for the result to be defined. Because `lane` is a runtime input from the compiler's perspective, the lowering must emit the lane-index broadcast in its general form: on AMD RDNA 2/3 that is `v_readlane_b32` (a SALU instruction) plus the bookkeeping required to broadcast back to VGPRs; on NVIDIA Turing and Ada Lovelace, the `SHFL.IDX` shuffle with a per-lane index argument; on Intel Xe-HPG, the SLM-backed lane-index path. None of these are expensive in absolute terms, but they all encode "the lane index is variable" and the compiler cannot strip the index plumbing.

## What the rule fires on

Calls of the form `WaveReadLaneAt(x, 0)` where the second argument is the integer literal `0` (or a `static const` integer that folds to zero at parse time). The match is on the literal lane index, not on the value of `x`. The rule does not fire on `WaveReadLaneAt(x, K)` for any non-zero compile-time constant `K` (that case has its own portability concern when wave size is not pinned and is queued as a Phase 3 reflection-aware rule, `wavereadlaneat-constant-non-zero-portability`), and does not fire on `WaveReadLaneAt(x, dynamic)` calls where the lane index is a runtime expression.

See the [What it detects](../rules/wavereadlaneat-constant-zero-to-readfirst.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavereadlaneat-constant-zero-to-readfirst.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavereadlaneat-constant-zero-to-readfirst.md -> Examples](../rules/wavereadlaneat-constant-zero-to-readfirst.md#examples).

## See also

- [Rule page](../rules/wavereadlaneat-constant-zero-to-readfirst.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
