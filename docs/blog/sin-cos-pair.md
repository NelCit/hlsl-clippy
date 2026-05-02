---
title: "sin-cos-pair"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: sin-cos-pair
---

# sin-cos-pair

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sin-cos-pair.md](../rules/sin-cos-pair.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On AMD RDNA/RDNA 2/RDNA 3, NVIDIA Turing/Ada Lovelace, and Intel Xe-HPG, `sin` and `cos` are transcendental instructions that run on the special-function unit (TALU / transcendental ALU) at one-quarter peak VALU throughput. Two separate calls â€” `sin(x)` and `cos(x)` â€” occupy two distinct quarter-rate issue slots: on RDNA 3 that is `v_sin_f32` followed by `v_cos_f32`, each at 1/4 rate, for a combined cost of roughly 8 full-rate ALU-equivalent cycles.

## What the rule fires on

Separate calls to `sin(x)` and `cos(x)` within the same function body where both calls share the same argument expression `x`. The rule matches any two calls â€” in any order, any number of statements apart â€” that operate on the same syntactic argument (same identifier, same literal, or structurally identical sub-expression). It does not fire when only one of the two is present, when the arguments differ, or when the results of both calls are already combined via a `sincos` intrinsic call.

See the [What it detects](../rules/sin-cos-pair.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sin-cos-pair.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sin-cos-pair.md -> Examples](../rules/sin-cos-pair.md#examples).

## See also

- [Rule page](../rules/sin-cos-pair.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
