---
title: "coopvec-non-uniform-matrix-handle"
date: 2026-05-02
author: hlsl-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-non-uniform-matrix-handle
---

# coopvec-non-uniform-matrix-handle

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-non-uniform-matrix-handle.md](../rules/coopvec-non-uniform-matrix-handle.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The cooperative-vector matrix engine on every supporting IHV (Ada tensor cores, RDNA 3/4 WMMA, Xe-HPG XMX) executes one matmul per wave, drawing operands from one source matrix per call. The matrix handle, offset, stride, and interpretation arguments parameterise that single matmul; the engine fetches operands once for the whole wave and broadcasts them to lanes.

## What the rule fires on

A cooperative-vector matrix-multiply call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose matrix-handle, base-offset, stride, or interpretation argument is wave-divergent. The SM 6.9 cooperative-vector spec marks these arguments as preferring uniform values; non-uniform arguments either serialise across the wave (perf) or, on stricter implementations, produce undefined behaviour. The Phase 4 uniformity analysis (shared with `wave-active-all-equal-precheck` and `cbuffer-divergent-index`) tracks divergence on each argument.

See the [What it detects](../rules/coopvec-non-uniform-matrix-handle.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-non-uniform-matrix-handle.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-non-uniform-matrix-handle.md -> Examples](../rules/coopvec-non-uniform-matrix-handle.md#examples).

## See also

- [Rule page](../rules/coopvec-non-uniform-matrix-handle.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
