---
title: "reference-data-type-not-supported-pre-sm610"
date: 2026-05-02
author: shader-clippy maintainers
category: sm6_10
tags: [hlsl, performance, sm6_10]
status: stub
related-rule: reference-data-type-not-supported-pre-sm610
---

# reference-data-type-not-supported-pre-sm610

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/reference-data-type-not-supported-pre-sm610.md](../rules/reference-data-type-not-supported-pre-sm610.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Reference data types are not yet shipped retail. Source compiled with the proposal-0006 syntax against SM 6.9 toolchains may compile-error or produce wrong code. The rule warns prospectively until the proposal ships.

## What the rule fires on

`<qual> ref <type>` parameter syntax (matching HLSL Specs proposal 0006, under-review) on a translation unit targeting SM 6.9 or older.

See the [What it detects](../rules/reference-data-type-not-supported-pre-sm610.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/reference-data-type-not-supported-pre-sm610.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[reference-data-type-not-supported-pre-sm610.md -> Examples](../rules/reference-data-type-not-supported-pre-sm610.md#examples).

## See also

- [Rule page](../rules/reference-data-type-not-supported-pre-sm610.md) -- canonical reference + change log.
- [sm6_10 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
