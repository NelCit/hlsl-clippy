---
title: "fromrayquery-invoke-without-shader-table: A `dx::HitObject::FromRayQuery(...)` value passed to `Invoke(...)` without an intervening `SetShaderTableIndex(...)` call on the same…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: fromrayquery-invoke-without-shader-table
---

# fromrayquery-invoke-without-shader-table

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/fromrayquery-invoke-without-shader-table.md](../rules/fromrayquery-invoke-without-shader-table.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`HitObject::FromRayQuery` constructs a HitObject from the result of an inline `RayQuery::TraceRayInline + Proceed` traversal. The inline traversal does not consult a shader binding table — that is the whole point of the inline path — so the resulting HitObject has no shader-table index field set. When the application then wants to invoke a hit / miss shader through the SER path, it needs to tell the runtime which shader to dispatch; that's `SetShaderTableIndex`.

## What the rule fires on

A `dx::HitObject::FromRayQuery(...)` value passed to `Invoke(...)` without an intervening `SetShaderTableIndex(...)` call on the same HitObject on every CFG path between construction and invocation. The SER spec requires `FromRayQuery`-constructed HitObjects to carry an explicit shader-table index before they can be invoked — the inline `RayQuery` does not record one because it has no concept of a shader table at traversal time. The Phase 4 definite-assignment analysis walks the CFG between construction and invocation.

See the [What it detects](../rules/fromrayquery-invoke-without-shader-table.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/fromrayquery-invoke-without-shader-table.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[fromrayquery-invoke-without-shader-table.md -> Examples](../rules/fromrayquery-invoke-without-shader-table.md#examples).

## See also

- [Rule page](../rules/fromrayquery-invoke-without-shader-table.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
