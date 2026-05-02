---
title: "coopvec-fp8-with-non-optimal-layout: A cooperative-vector matrix multiply whose interpretation enum names an FP8 component type (`COMPONENT_TYPE_FLOAT_E4M3`, `COMPONENT_TYPE_FLOAT_E5M2`)…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-fp8-with-non-optimal-layout
---

# coopvec-fp8-with-non-optimal-layout

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-fp8-with-non-optimal-layout.md](../rules/coopvec-fp8-with-non-optimal-layout.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

FP8 (E4M3 and E5M2) is the SM 6.9 cooperative-vector path's lowest-precision data type and the one with the highest throughput on the tensor engines. NVIDIA Ada Lovelace's tensor cores execute FP8 matmul at roughly 2x the FP16 throughput; AMD RDNA 3/4's WMMA-FP8 path matches; Intel Xe-HPG's XMX engines have the same approximate ratio. The throughput advantage requires the engine's native FP8 storage layout — the hardware fetcher assumes the bytes are pre-arranged in the swizzle pattern that lets one fetch deliver one tensor-core operand.

## What the rule fires on

A cooperative-vector matrix multiply whose interpretation enum names an FP8 component type (`COMPONENT_TYPE_FLOAT_E4M3`, `COMPONENT_TYPE_FLOAT_E5M2`) and whose matrix-layout enum is *not* one of the optimal layouts (`MATRIX_LAYOUT_INFERENCING_OPTIMAL` / `MATRIX_LAYOUT_TRAINING_OPTIMAL`). The SM 6.9 cooperative-vector specification mandates an optimal layout for FP8 matrices: the tensor-engine's FP8 path on every IHV requires the vendor-swizzle layout to function correctly. Generic row-major / column-major FP8 is undefined behaviour.

See the [What it detects](../rules/coopvec-fp8-with-non-optimal-layout.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-fp8-with-non-optimal-layout.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-fp8-with-non-optimal-layout.md -> Examples](../rules/coopvec-fp8-with-non-optimal-layout.md#examples).

## See also

- [Rule page](../rules/coopvec-fp8-with-non-optimal-layout.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
