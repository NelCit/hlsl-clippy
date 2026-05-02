---
title: "wavereadlaneat-constant-non-zero-portability"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: wavereadlaneat-constant-non-zero-portability
---

# wavereadlaneat-constant-non-zero-portability

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavereadlaneat-constant-non-zero-portability.md](../rules/wavereadlaneat-constant-non-zero-portability.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`WaveReadLaneAt(x, K)` returns the value of `x` from lane `K` within the current wave. The valid range of `K` is `[0, WaveGetLaneCount() - 1]`. On AMD RDNA 1/2/3 the hardware supports both 32-wide and 64-wide waves; the driver picks one per-PSO based on hints and register pressure. On NVIDIA Turing and Ada Lovelace the wave is always 32 lanes. On Intel Xe-HPG the SIMD width is 8, 16, or 32 lanes depending on the compiler's choice. A `WaveReadLaneAt(x, 47)` is valid on RDNA wave64, undefined on RDNA wave32 / Ada (lane index out of range), and undefined on Xe-HPG wave16 / wave32. The HLSL spec says out-of-range lane indices produce undefined results â€” in practice the hardware returns garbage or zero depending on the IHV.

## What the rule fires on

A `WaveReadLaneAt(x, K)` call where `K` is a compile-time non-zero constant in an entry point that lacks a `[WaveSize(N)]` (or `[WaveSize(min, max)]`) attribute pinning the wave size. The detector folds the second argument to a constant if possible, reads the entry's `[WaveSize]` attribute via reflection, and fires when the constant is `>= 32` (potentially out-of-range on a 32-wide wave) or, more tightly, when the constant is `>= min(supported_wave_sizes_on_target)`. The companion AST-only rule `wavereadlaneat-constant-zero-to-readfirst` (Phase 2) handles the `K == 0` case; this rule handles `K != 0`.

See the [What it detects](../rules/wavereadlaneat-constant-non-zero-portability.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavereadlaneat-constant-non-zero-portability.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavereadlaneat-constant-non-zero-portability.md -> Examples](../rules/wavereadlaneat-constant-non-zero-portability.md#examples).

## See also

- [Rule page](../rules/wavereadlaneat-constant-non-zero-portability.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
