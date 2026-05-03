---
title: "redundant-saturate"
date: 2026-05-02
author: shader-clippy maintainers
category: saturate-redundancy
tags: [hlsl, performance, saturate-redundancy]
status: stub
related-rule: redundant-saturate
---

# redundant-saturate

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-saturate.md](../rules/redundant-saturate.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On AMD RDNA, RDNA 2, and RDNA 3 hardware, `saturate` is not an independent instruction. It is an output modifier bit (`_clamp`) that is folded into whichever ALU instruction produces the value â€” an ADD, MUL, MAD, FMA, or similar â€” at zero additional cycle cost. The compiler can attach `_clamp` to the last instruction that writes the register and the hardware enforces the [0, 1] clamp during writeback with no extra cycles.

## What the rule fires on

Calls of the form `saturate(saturate(x))` where the outer `saturate` is applied to an expression already guaranteed to be in [0, 1] because it is itself a `saturate` call. The rule matches both the direct nested form â€” `saturate(saturate(expr))` â€” and the split-variable form where a `saturate` result is stored in an intermediate variable and then passed to a second `saturate` (see lines 8-11 of `tests/fixtures/phase2/redundant.hlsl`). It does not fire when the argument to the outer `saturate` could originate from any source other than a prior `saturate`.

See the [What it detects](../rules/redundant-saturate.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-saturate.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-saturate.md -> Examples](../rules/redundant-saturate.md#examples).

## See also

- [Rule page](../rules/redundant-saturate.md) -- canonical reference + change log.
- [saturate-redundancy overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
