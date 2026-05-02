---
title: "oriented-bbox-not-set-on-rdna4: A defensive informational rule. Fires once per source that contains any RT call (`TraceRay`,…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: rdna4
tags: [hlsl, performance, rdna4]
status: stub
related-rule: oriented-bbox-not-set-on-rdna4
---

# oriented-bbox-not-set-on-rdna4

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/oriented-bbox-not-set-on-rdna4.md](../rules/oriented-bbox-not-set-on-rdna4.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Per Chips and Cheese's RDNA 4 raytracing deep-dive, RDNA 4 gains up to 10% RT performance when the BLAS is built with the `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` (or VK equivalent) flag. The flag is project-side state -- we cannot inspect it from shader source -- so the rule emits a one-time-per-source informational note pointing the developer to verify their BLAS-build code.

## What the rule fires on

A defensive informational rule. Fires once per source that contains any RT call (`TraceRay`, `RayQuery::Proceed`, `TraceRayInline`) under the `[experimental.target = rdna4]` config gate.

See the [What it detects](../rules/oriented-bbox-not-set-on-rdna4.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/oriented-bbox-not-set-on-rdna4.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[oriented-bbox-not-set-on-rdna4.md -> Examples](../rules/oriented-bbox-not-set-on-rdna4.md#examples).

## See also

- [Rule page](../rules/oriented-bbox-not-set-on-rdna4.md) -- canonical reference + change log.
- [rdna4 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*

**TODO:** category-overview missing for `rdna4`; linked overview is the closest sibling.