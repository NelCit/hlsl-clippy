---
title: "acos-without-saturate"
date: 2026-05-02
author: shader-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: acos-without-saturate
---

# acos-without-saturate

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/acos-without-saturate.md](../rules/acos-without-saturate.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The IEEE 754 specification for `acos` returns NaN for any argument outside `[-1, 1]`. Hardware implementations on AMD RDNA 2/3 (`v_acos_f32`), NVIDIA Turing/Ada (`MUFU.RCOS` family), and Intel Xe-HPG (the transcendental pipe) faithfully implement this: feed `1.0 + 1e-7` and you get a NaN, which then propagates through every subsequent math op into the final colour write. A single NaN pixel in a deferred shading buffer corrupts every subsequent neighbourhood operation (TAA, denoisers, blur), often manifesting as a black or fluorescent-pink dot that grows over a few frames. This is one of the most common GPU correctness bugs in production rendering code.

## What the rule fires on

Calls to `acos(x)` (and `asin(x)`) where the argument is the result of a `dot(a, b)` between two vectors that are not both provably unit-length, or any other expression whose value is mathematically in `[-1, 1]` but, due to floating-point rounding, can land just outside that range. The canonical pattern is `acos(dot(normalize(a), normalize(b)))` where the two `normalize` calls produce vectors whose dot product can round to `1.0 + epsilon` or `-1.0 - epsilon`; `acos` of an out-of-domain argument returns NaN.

See the [What it detects](../rules/acos-without-saturate.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/acos-without-saturate.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[acos-without-saturate.md -> Examples](../rules/acos-without-saturate.md#examples).

## See also

- [Rule page](../rules/acos-without-saturate.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
