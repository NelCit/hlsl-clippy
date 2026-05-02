---
title: "mul-identity: Arithmetic expressions where one operand is a numeric literal that makes the operation trivially…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: mul-identity
---

# mul-identity

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mul-identity.md](../rules/mul-identity.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Every VALU instruction on a GPU occupies an issue slot. On AMD RDNA 3, the VALU can sustain one FP32 instruction per clock per SIMD32 lane at peak throughput. A multiply-by-one issues an FP32 `v_mul_f32` that reads two source registers, executes a multiply, and writes back — doing mathematically zero work. A GPU compiler may eliminate these in an early simplification pass, but only when the constant is visible and the pass runs before register allocation. Across function boundaries, after constant-buffer loads, or in generated shader code from offline material compilers, this simplification frequently does not happen at the HLSL level.

## What the rule fires on

Arithmetic expressions where one operand is a numeric literal that makes the operation trivially reducible:

See the [What it detects](../rules/mul-identity.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mul-identity.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mul-identity.md -> Examples](../rules/mul-identity.md#examples).

## See also

- [Rule page](../rules/mul-identity.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
