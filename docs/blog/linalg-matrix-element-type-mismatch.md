---
title: "linalg-matrix-element-type-mismatch: A `linalg::*Mul` chain whose matrix element type (e.g. `COMPONENT_TYPE_FLOAT16`, `COMPONENT_TYPE_FLOAT_E4M3`) is mixed with a…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: linalg
tags: [hlsl, performance, linalg]
status: stub
related-rule: linalg-matrix-element-type-mismatch
---

# linalg-matrix-element-type-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/linalg-matrix-element-type-mismatch.md](../rules/linalg-matrix-element-type-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The matrix-engine fetcher silently widens the matrix's elements to the accumulator's precision, performing a per-element conversion that costs throughput on every IHV's matrix engine (Blackwell 5th-gen Tensor Cores, RDNA 4 AI accelerator, Xe2 XMX, Hopper Tensor Cores). Operations that look free in code are paid for at the fetcher.

## What the rule fires on

A `linalg::*Mul` chain whose matrix element type (e.g. `COMPONENT_TYPE_FLOAT16`, `COMPONENT_TYPE_FLOAT_E4M3`) is mixed with a high-precision accumulator (`COMPONENT_TYPE_FLOAT32` / `_FLOAT64`) without an explicit conversion. Activates only on SM 6.10+ targets.

See the [What it detects](../rules/linalg-matrix-element-type-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/linalg-matrix-element-type-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[linalg-matrix-element-type-mismatch.md -> Examples](../rules/linalg-matrix-element-type-mismatch.md#examples).

## See also

- [Rule page](../rules/linalg-matrix-element-type-mismatch.md) -- canonical reference + change log.
- [linalg overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
