---
title: "isnormal-pre-sm69: A call to the `isnormal` intrinsic in a shader compiled against a target shader…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: isnormal-pre-sm69
---

# isnormal-pre-sm69

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/isnormal-pre-sm69.md](../rules/isnormal-pre-sm69.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`isnormal` is a portable numerical-classification primitive that mirrors the C99 `isnormal` semantic. Before SM 6.9, authors hand-rolled the equivalent check using bit-cast tricks: `asuint(x) & 0x7F800000` against the IEEE exponent field, with separate tests for zero and subnormal. The hand-rolled form costs 4-6 instructions, plus a wave-wide bit-mask construction; the SM 6.9 intrinsic compiles to a single per-lane test on every IHV (NVIDIA Ada Lovelace, AMD RDNA 3/4, Intel Xe-HPG) because the floating-point classification logic is exposed by the underlying ISA.

## What the rule fires on

A call to the `isnormal` intrinsic in a shader compiled against a target shader model older than SM 6.9. The `isnormal` intrinsic — which returns true when the argument is a normal IEEE float (not zero, not subnormal, not infinity, not NaN) — was added in SM 6.9. Earlier targets do not implement it and DXC issues a hard compile error. Slang reflection provides the target SM; the rule reads the target and fires when `isnormal` is called against any pre-SM-6.9 target.

See the [What it detects](../rules/isnormal-pre-sm69.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/isnormal-pre-sm69.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[isnormal-pre-sm69.md -> Examples](../rules/isnormal-pre-sm69.md#examples).

## See also

- [Rule page](../rules/isnormal-pre-sm69.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
