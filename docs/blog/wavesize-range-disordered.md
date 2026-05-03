---
title: "wavesize-range-disordered"
date: 2026-05-02
author: shader-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: wavesize-range-disordered
---

# wavesize-range-disordered

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavesize-range-disordered.md](../rules/wavesize-range-disordered.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`[WaveSize]` is the contract between the shader author and the compiler about which wave widths the shader is willing to run on. The compiler uses the bounds to pick an instruction-selection strategy (RDNA 1/2/3 can run wave32 or wave64 depending on the kernel; NVIDIA Turing/Ada is fixed at wave32; Intel Xe-HPG runs wave8/16/32 depending on register pressure). When the bounds are well-ordered, the runtime picks an in-range wave size at dispatch time and the shader runs as designed.

## What the rule fires on

A `[WaveSize(min, preferred, max)]` or `[WaveSize(min, max)]` attribute whose constant arguments are not in non-decreasing order. The SM 6.8 attribute requires `min <= preferred <= max`; the SM 6.6 two-arg form requires `min <= max`. Constant-fold the integer arguments and fire when the ordering is violated. Common authoring slip: a copy-paste of `[WaveSize(64, 32)]` from a wave64-then-wave32 RDNA snippet.

See the [What it detects](../rules/wavesize-range-disordered.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavesize-range-disordered.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavesize-range-disordered.md -> Examples](../rules/wavesize-range-disordered.md#examples).

## See also

- [Rule page](../rules/wavesize-range-disordered.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
