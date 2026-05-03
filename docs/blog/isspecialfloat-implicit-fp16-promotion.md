---
title: "isspecialfloat-implicit-fp16-promotion"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: isspecialfloat-implicit-fp16-promotion
---

# isspecialfloat-implicit-fp16-promotion

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/isspecialfloat-implicit-fp16-promotion.md](../rules/isspecialfloat-implicit-fp16-promotion.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

SM 6.9 added the explicit numerical-classification intrinsics to give authors a portable way to test for IEEE special values. The intrinsics are defined for `float` and `double`; the fp16 path was left to the compiler's promotion rules in the early SM 6.9 drafts and is being tightened in the spec revisions. On NVIDIA Ada Lovelace, the fp16 promotion path costs an extra `cvt.f32.f16` instruction per test on a wave that would otherwise be packed-fp16; on AMD RDNA 3, the WMMA-fp16 lanes are unpacked to scalar fp32 lanes for the test, defeating the packed-math savings. Intel Xe-HPG behaves the same way.

## What the rule fires on

A call to one of the SM 6.9 numerical-classification intrinsics (`isnan`, `isinf`, `isfinite`, `isnormal`) with an `fp16` (`half` / `min16float` / `float16_t`) argument compiled against a target where the intrinsic is not implemented natively for fp16, causing the compiler to silently promote the argument to `float` before testing. Slang reflection provides the target SM and the argument type; the rule fires when an fp16 argument widens implicitly.

See the [What it detects](../rules/isspecialfloat-implicit-fp16-promotion.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/isspecialfloat-implicit-fp16-promotion.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[isspecialfloat-implicit-fp16-promotion.md -> Examples](../rules/isspecialfloat-implicit-fp16-promotion.md#examples).

## See also

- [Rule page](../rules/isspecialfloat-implicit-fp16-promotion.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
