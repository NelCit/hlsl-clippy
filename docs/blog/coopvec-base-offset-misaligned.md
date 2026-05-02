---
title: "coopvec-base-offset-misaligned: A cooperative-vector matrix-load call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose constant-folded `offset` argument is not aligned…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-base-offset-misaligned
---

# coopvec-base-offset-misaligned

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-base-offset-misaligned.md](../rules/coopvec-base-offset-misaligned.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Tensor / matrix engines on every IHV require their source operands to be aligned because the engine's load unit is wired for vector-width transactions. NVIDIA Ada Lovelace's tensor cores fetch operands in 128-bit-aligned groups; AMD RDNA 3/4 WMMA loads through a 128-bit-aligned scalar path; Intel Xe-HPG XMX engines align to the SIMD width. A misaligned base offset either splits the fetch into two transactions (cutting throughput in half) or, on stricter implementations, faults the load — the cooperative-vector spec writes the latter as undefined behaviour to give IHVs the freedom to fail loudly.

## What the rule fires on

A cooperative-vector matrix-load call (`MatrixMul`, `MatrixVectorMul`, `OuterProductAccumulate`) whose constant-folded `offset` argument is not aligned to the cooperative-vector spec's mandated alignment for the chosen component type and layout (typically 16 bytes for float / FP16 / BF16 paths, 64 bytes for the OPTIMAL layouts on most IHVs). The rule walks the constant-fold chain on the offset argument and the alignment annotation surfaced via Slang reflection, then fires on misalignment.

See the [What it detects](../rules/coopvec-base-offset-misaligned.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-base-offset-misaligned.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-base-offset-misaligned.md -> Examples](../rules/coopvec-base-offset-misaligned.md#examples).

## See also

- [Rule page](../rules/coopvec-base-offset-misaligned.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
