---
title: "redundant-transpose: Calls of the form `transpose(transpose(M))` where a matrix is transposed twice, yielding the original…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: saturate-redundancy
tags: [hlsl, performance, saturate-redundancy]
status: stub
related-rule: redundant-transpose
---

# redundant-transpose

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-transpose.md](../rules/redundant-transpose.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`transpose` in HLSL is a pure value operation: it rearranges the rows and columns of a matrix without any mathematical computation beyond register reassignment. In an ideal pipeline — when the compiler can see both `transpose` calls in the same function and the matrix is stored in contiguous VGPRs — the two calls cancel out and no instructions are emitted. However, this cancellation is not guaranteed across function-call boundaries, across inlining decisions, or when the matrix is passed through a constant buffer or read from a structured buffer.

## What the rule fires on

Calls of the form `transpose(transpose(M))` where a matrix is transposed twice, yielding the original matrix. The rule matches both the direct nested form and the split-variable form where the result of a `transpose` is stored in an intermediate variable and then passed to a second `transpose`. It does not fire unless both calls are verifiably operating on the same matrix type (same row and column counts), and does not fire on non-square matrices where a type mismatch would make the double-transpose structurally visible. It does fire on `float2x2`, `float3x3`, `float4x4`, and their `halfN` equivalents.

See the [What it detects](../rules/redundant-transpose.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-transpose.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-transpose.md -> Examples](../rules/redundant-transpose.md#examples).

## See also

- [Rule page](../rules/redundant-transpose.md) -- canonical reference + change log.
- [saturate-redundancy overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
