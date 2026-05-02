---
title: "clamp01-to-saturate"
date: 2026-05-02
author: hlsl-clippy maintainers
category: saturate-redundancy
tags: [hlsl, performance, saturate-redundancy]
status: stub
related-rule: clamp01-to-saturate
---

# clamp01-to-saturate

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/clamp01-to-saturate.md](../rules/clamp01-to-saturate.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`clamp(x, 0.0, 1.0)` is defined by the HLSL specification as `min(max(x, 0.0), 1.0)`. Without further optimisation, the compiler lowers this to two separate ALU instructions: a `max` (or `v_max_f32` on RDNA, `FMAX` on NVIDIA) followed by a `min` (`v_min_f32`, `FMIN`). Each instruction occupies a VALU issue slot. Even if the two ops are scheduled back-to-back, they represent a real two-cycle dependency chain on any hardware where a VALU result must be written back to the VGPR file before the next instruction can read it.

## What the rule fires on

Calls to `clamp(x, 0.0, 1.0)` â€” or the equivalent integer-literal form `clamp(x, 0, 1)` â€” where both the lower and upper bounds are compile-time constants equal to exactly 0.0 and 1.0 respectively, after type coercion. The rule fires on scalar, vector, and matrix operands. It does not fire when either bound is a variable, a constant buffer field, or any expression that is not a numeric literal, even if the value happens to evaluate to 0 or 1 at runtime.

See the [What it detects](../rules/clamp01-to-saturate.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/clamp01-to-saturate.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[clamp01-to-saturate.md -> Examples](../rules/clamp01-to-saturate.md#examples).

## See also

- [Rule page](../rules/clamp01-to-saturate.md) -- canonical reference + change log.
- [saturate-redundancy overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
