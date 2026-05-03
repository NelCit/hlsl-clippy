---
title: "groupshared-over-32k-without-attribute"
date: 2026-05-02
author: shader-clippy maintainers
category: sm6_10
tags: [hlsl, performance, sm6_10]
status: stub
related-rule: groupshared-over-32k-without-attribute
---

# groupshared-over-32k-without-attribute

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-over-32k-without-attribute.md](../rules/groupshared-over-32k-without-attribute.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Per HLSL Specs proposal 0049 (Accepted), the SM 6.10 default groupshared cap is 32 KB; exceeding it requires the `[GroupSharedLimit(N)]` attribute. On SM 6.10 retail this compile-errors; on SM <= 6.9 the LDS allocation is silently truncated, producing out-of-bounds writes in groupshared. RDNA 3 and Turing/Ada have 64 KB / 100 KB / 128 KB caps respectively that the attribute opts the kernel into; without it the driver enforces the conservative ceiling.

## What the rule fires on

Total `groupshared` allocation in a translation unit exceeding the SM 6.10 default of 32 KB without a `[GroupSharedLimit(<bytes>)]` attribute on the entry point.

See the [What it detects](../rules/groupshared-over-32k-without-attribute.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-over-32k-without-attribute.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-over-32k-without-attribute.md -> Examples](../rules/groupshared-over-32k-without-attribute.md#examples).

## See also

- [Rule page](../rules/groupshared-over-32k-without-attribute.md) -- canonical reference + change log.
- [sm6_10 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
