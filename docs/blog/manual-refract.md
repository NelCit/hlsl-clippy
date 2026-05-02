---
title: "manual-refract"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: manual-refract
---

# manual-refract

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-refract.md](../rules/manual-refract.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The hand-rolled `refract` body decomposes into roughly ten dependent VALU operations on every consumer-class GPU. On AMD **RDNA 2 / RDNA 3** the `dot` lowers to a `v_dot3_f32` (or a pair of `v_fma_f32` ops on architectures without packed dot), each scalar multiply and the trailing vector subtract sit on the regular VALU pipe at one issue per cycle, and the `sqrt` lowers to a transcendental issued through the SFU at one-quarter VALU rate. The chain is dependency-bound: the `sqrt` cannot start until the `dot * dot * eta * eta` chain that feeds `k` retires, and the final vector multiply against `N` cannot start until `sqrt` retires. On NVIDIA **Turing**, **Ampere**, and **Ada Lovelace** the same story plays out on the SM's special-function unit, which runs at 1/4 the FP32 rate. The hand-rolled form therefore stalls on the SFU for `sqrt` even though the surrounding VALU is idle â€” a textbook latency-bound pattern.

## What the rule fires on

A return statement whose expression structurally matches the closed-form HLSL implementation of `refract(I, N, eta)`:

See the [What it detects](../rules/manual-refract.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-refract.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-refract.md -> Examples](../rules/manual-refract.md#examples).

## See also

- [Rule page](../rules/manual-refract.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
