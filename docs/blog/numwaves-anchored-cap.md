---
title: "numwaves-anchored-cap"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: numwaves-anchored-cap
---

# numwaves-anchored-cap

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/numwaves-anchored-cap.md](../rules/numwaves-anchored-cap.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The current per-group lane cap is 1024 across every modern IHV (D3D12 spec limit). Exceeding it is a hard validator error in DXC -- but the proposal 0054 horizon adds a `numWaves` attribute that may relax this. Until 0054 ships, exceedance is an error worth flagging early.

## What the rule fires on

A `[numthreads(X, Y, Z)]` declaration where `X * Y * Z > 1024` (the current per-thread-group lane cap). Defensive rule for HLSL Specs proposal 0054 (`numWaves`, under-consideration).

See the [What it detects](../rules/numwaves-anchored-cap.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/numwaves-anchored-cap.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[numwaves-anchored-cap.md -> Examples](../rules/numwaves-anchored-cap.md#examples).

## See also

- [Rule page](../rules/numwaves-anchored-cap.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
