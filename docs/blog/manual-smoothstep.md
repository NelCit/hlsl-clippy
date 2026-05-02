---
title: "manual-smoothstep"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-smoothstep
---

# manual-smoothstep

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-smoothstep.md](../rules/manual-smoothstep.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The cubic Hermite polynomial `t*t*(3 - 2*t)` is 5 FP32 operations for a scalar input: one multiply (for `t*t`), one multiply-by-2, one subtract, one multiply (for `t*t * (3 - 2*t)`). The preceding `saturate((x - edge0) / (edge1 - edge0))` adds a subtract, a subtract, a division (or reciprocal-multiply), and a saturate â€” roughly 4 more operations, with the division typically being a 2-cycle `v_rcp_f32` sequence. Total: approximately 10 scalar FP32 instructions.

## What the rule fires on

A hand-rolled cubic Hermite interpolation that implements the body of the `smoothstep` intrinsic. Specifically, the rule matches the sequence:

See the [What it detects](../rules/manual-smoothstep.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-smoothstep.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-smoothstep.md -> Examples](../rules/manual-smoothstep.md#examples).

## See also

- [Rule page](../rules/manual-smoothstep.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
