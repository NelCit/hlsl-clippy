---
title: "ser-trace-then-invoke-without-reorder"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: ser-trace-then-invoke-without-reorder
---

# ser-trace-then-invoke-without-reorder

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/ser-trace-then-invoke-without-reorder.md](../rules/ser-trace-then-invoke-without-reorder.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The whole point of using `dx::HitObject::TraceRay` + `Invoke` instead of plain `TraceRay` is to give the runtime an opportunity to reorder lanes before the closest-hit / miss shader runs. If the application constructs a HitObject and invokes it immediately without calling `MaybeReorderThread`, the runtime gets no reorder opportunity â€” it dispatches the shaders in whatever order the lanes happen to land, exactly as plain `TraceRay` would. The HitObject machinery (which has its own per-lane register-spill cost on every IHV: NVIDIA Ada Lovelace, AMD RDNA 4 when shipped, Intel Xe-HPG when shipped) is paid for and discarded.

## What the rule fires on

A `dx::HitObject::TraceRay` (or `FromRayQuery`) construction whose only use is a direct `Invoke` call on the same HitObject, without an intervening `dx::MaybeReorderThread`. The Phase 4 reachability analysis walks from the construction site to the invocation site and verifies that no `MaybeReorderThread` exists on any path. This is the missed-opportunity counterpart to the SER programming-model rules.

See the [What it detects](../rules/ser-trace-then-invoke-without-reorder.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/ser-trace-then-invoke-without-reorder.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[ser-trace-then-invoke-without-reorder.md -> Examples](../rules/ser-trace-then-invoke-without-reorder.md#examples).

## See also

- [Rule page](../rules/ser-trace-then-invoke-without-reorder.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
