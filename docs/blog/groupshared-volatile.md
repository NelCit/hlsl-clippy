---
title: "groupshared-volatile"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-volatile
---

# groupshared-volatile

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-volatile.md](../rules/groupshared-volatile.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

HLSL's memory model (DXC and Slang both follow the DXIL spec on this) treats `volatile` as a hint to suppress *intra-thread* reordering of loads and stores â€” the same per-thread guarantee C inherits from its origin as a systems language. It says nothing about cross-thread visibility. On `groupshared` storage, where the entire reason a variable exists is to communicate between threads in a workgroup, this guarantee is exactly the wrong one. Programmers reach for `volatile` when they want "every thread sees the latest value"; they get the per-thread reordering fence and *no* cross-thread synchronisation. The actual mechanisms HLSL provides for that are `GroupMemoryBarrierWithGroupSync()` (or `GroupMemoryBarrier()` for the no-execution-sync variant) and, for UAV traffic, the `globallycoherent` storage class.

## What the rule fires on

Declarations of the form `volatile groupshared T name[...];` (or any other ordering of the `volatile` and `groupshared` storage qualifiers on the same declaration). The match is purely structural â€” the rule fires whenever the `volatile` keyword appears in the qualifier list of a declaration that also carries `groupshared`. It does not look at how the variable is subsequently used; the `volatile` is meaningless on `groupshared` storage regardless of access pattern. The rule does not fire on plain `volatile` locals (no `groupshared`) because those occasionally have legitimate uses around inline assemblyâ€“style spinning, nor on `globallycoherent` UAV declarations, which are the *real* mechanism HLSL provides for cross-thread visibility.

See the [What it detects](../rules/groupshared-volatile.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-volatile.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-volatile.md -> Examples](../rules/groupshared-volatile.md#examples).

## See also

- [Rule page](../rules/groupshared-volatile.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
