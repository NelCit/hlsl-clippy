---
title: "redundant-precision-cast"
date: 2026-05-02
author: shader-clippy maintainers
category: misc
tags: [hlsl, performance, misc]
status: stub
related-rule: redundant-precision-cast
---

# redundant-precision-cast

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-precision-cast.md](../rules/redundant-precision-cast.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Each type conversion â€” `v_cvt_f32_i32`, `v_cvt_i32_f32`, `v_cvt_f32_f16`, `v_cvt_f16_f32` on RDNA; the equivalent `FCONV`/`I2F`/`F2I` family on Turing and Xe-HPG â€” is a real ALU instruction. Pairs of such instructions in a round-trip pattern consume two instruction-issue slots and two VGPR reads/writes. On RDNA 3, conversion instructions execute in the VALU pipeline at full throughput, so a two-instruction round-trip costs two cycles of VALU occupancy per lane â€” identical in cost to two FP32 multiplies â€” for zero arithmetic progress.

## What the rule fires on

Nested cast expressions that form precision-degrading or no-op round-trips. Three specific patterns are detected:

See the [What it detects](../rules/redundant-precision-cast.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-precision-cast.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-precision-cast.md -> Examples](../rules/redundant-precision-cast.md#examples).

## See also

- [Rule page](../rules/redundant-precision-cast.md) -- canonical reference + change log.
- [misc overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*

**TODO:** category-overview missing for `misc`; linked overview is the closest sibling.