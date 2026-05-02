---
title: "redundant-abs"
date: 2026-05-02
author: hlsl-clippy maintainers
category: saturate-redundancy
tags: [hlsl, performance, saturate-redundancy]
status: stub
related-rule: redundant-abs
---

# redundant-abs

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-abs.md](../rules/redundant-abs.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`abs` in HLSL is not a free operation when applied to an arbitrary VGPR value. On AMD RDNA, RDNA 2, and RDNA 3, `abs` can be encoded as a source modifier bit on an instruction that reads the value â€” similar to how `neg` works â€” but only when the `abs` is the direct input to another ALU instruction that supports the modifier. When `abs(expr)` is used as a standalone expression (assigned to a variable, returned, or passed to a function), and the compiler cannot fold it into the consuming instruction, it must emit an explicit instruction. The typical lowering is `v_max_f32 dst, src, -src` (which computes `max(x, -x)`), occupying a full VALU slot.

## What the rule fires on

Calls to `abs(expr)` where the enclosed expression is statically provable to be non-negative. The rule currently recognises three sub-patterns:

See the [What it detects](../rules/redundant-abs.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-abs.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-abs.md -> Examples](../rules/redundant-abs.md#examples).

## See also

- [Rule page](../rules/redundant-abs.md) -- canonical reference + change log.
- [saturate-redundancy overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
