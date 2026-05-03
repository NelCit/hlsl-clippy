---
title: "coopvec-stride-mismatch"
date: 2026-05-02
author: shader-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-stride-mismatch
---

# coopvec-stride-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-stride-mismatch.md](../rules/coopvec-stride-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

When a cooperative-vector call uses a generic row-major or column-major layout, the matrix engine on each IHV (Ada tensor cores, RDNA 3/4 WMMA, Xe-HPG XMX) walks the source buffer using the stride argument as the per-row byte advance. The engine assumes the stride is the natural one for the matrix shape and component type; if it isn't, the engine reads garbage bytes from outside the matrix or from the wrong row, and produces NaN-laced or zero results. There is no error signalled at runtime â€” the tensor engine has no concept of buffer bounds beyond what the stride tells it.

## What the rule fires on

A cooperative-vector matrix-load call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose constant-folded `stride` argument does not equal the natural row-stride implied by the matrix dimensions and the component type (`rows * sizeof(component)` or `cols * sizeof(component)` depending on layout). The SM 6.9 cooperative-vector specification requires the stride to match the matrix layout exactly when the layout enum is not OPTIMAL; mismatches produce undefined behaviour because the matrix-engine fetcher walks the wrong number of bytes per row.

See the [What it detects](../rules/coopvec-stride-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-stride-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-stride-mismatch.md -> Examples](../rules/coopvec-stride-mismatch.md#examples).

## See also

- [Rule page](../rules/coopvec-stride-mismatch.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
