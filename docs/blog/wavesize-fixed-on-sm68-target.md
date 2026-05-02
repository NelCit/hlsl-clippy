---
title: "wavesize-fixed-on-sm68-target"
date: 2026-05-02
author: hlsl-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: wavesize-fixed-on-sm68-target
---

# wavesize-fixed-on-sm68-target

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavesize-fixed-on-sm68-target.md](../rules/wavesize-fixed-on-sm68-target.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The SM 6.6 fixed `[WaveSize(N)]` attribute pins the kernel to exactly one wave size: the runtime will refuse to dispatch the kernel on devices whose available wave sizes don't include N. On AMD RDNA 1/2/3 (which can run wave32 or wave64), pinning to wave64 means the kernel cannot run when the driver's heuristic prefers wave32, and vice versa. NVIDIA Turing/Ada Lovelace is fixed at wave32, so a `[WaveSize(64)]` kernel is unrunnable there. Intel Xe-HPG can run wave8/16/32 depending on register pressure; pinning to a single value forces a sub-optimal allocation.

## What the rule fires on

A fixed-form `[WaveSize(N)]` attribute on a kernel compiled against an SM 6.8 (or later) target, where the SM 6.8 range form `[WaveSize(min, max)]` or three-arg form `[WaveSize(min, preferred, max)]` would let the runtime pick an in-range wave size that better fits the dispatch. Slang reflection provides the target shader model; the rule fires on `[WaveSize(N)]` against SM 6.8+ targets.

See the [What it detects](../rules/wavesize-fixed-on-sm68-target.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavesize-fixed-on-sm68-target.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavesize-fixed-on-sm68-target.md -> Examples](../rules/wavesize-fixed-on-sm68-target.md#examples).

## See also

- [Rule page](../rules/wavesize-fixed-on-sm68-target.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
