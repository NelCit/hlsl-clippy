---
title: "maybereorderthread-without-payload-shrink: A `dx::MaybeReorderThread(...)` call whose surrounding payload struct contains live state that is *not read*…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: maybereorderthread-without-payload-shrink
---

# maybereorderthread-without-payload-shrink

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/maybereorderthread-without-payload-shrink.md](../rules/maybereorderthread-without-payload-shrink.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

SER's runtime spills the entire ray-payload at the reorder point: `MaybeReorderThread` reorganises lanes, and the per-lane state (the payload, plus any caller-side live registers) has to follow each lane to its new position. NVIDIA's Indiana Jones path-tracer case study quantified this: the reorder's spill traffic is proportional to live-state size, and the case study reported 10-25% perf gains by shrinking the payload from 64 bytes to 16 bytes around the reorder, even when the larger payload was needed before and after.

## What the rule fires on

A `dx::MaybeReorderThread(...)` call whose surrounding payload struct contains live state that is *not read* after the reorder, i.e., values written before the reorder, not consumed inside the reorder's downstream invocation, and not read after. The Phase 7 IR-level live-range analysis (shared with `live-state-across-traceray`) walks per-lane lifetimes across the reorder and identifies fields that are dead across the call.

See the [What it detects](../rules/maybereorderthread-without-payload-shrink.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/maybereorderthread-without-payload-shrink.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[maybereorderthread-without-payload-shrink.md -> Examples](../rules/maybereorderthread-without-payload-shrink.md#examples).

## See also

- [Rule page](../rules/maybereorderthread-without-payload-shrink.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
