---
title: "cbuffer-load-in-loop"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: cbuffer-load-in-loop
---

# cbuffer-load-in-loop

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cbuffer-load-in-loop.md](../rules/cbuffer-load-in-loop.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Cbuffer data resides in a dedicated constant buffer cache â€” a small, high-bandwidth, read-only cache separate from L1/L2 texture and UAV caches. On AMD RDNA and RDNA 2/3, the constant cache (also called the scalar cache or K-cache) is accessed via the scalar register file (SGPRs). The hardware is architecturally designed for the case where every lane in a wave reads the same cbuffer value simultaneously, which it does for any truly uniform constant: one scalar load fills an SGPR, and that SGPR value is broadcast to all 32 or 64 lanes without consuming per-lane VGPR space. In practice, the cbuffer value is loaded into an SGPR once per wave (or per draw call in the driver's implementation) and cached there â€” no repeated cache requests occur.

## What the rule fires on

Reads from a `cbuffer` (or `ConstantBuffer<T>`) field â€” or any arithmetic expression whose operands are exclusively cbuffer fields and numeric literals â€” inside a loop body, when the expression is loop-invariant: it does not depend on the loop induction variable or any value defined inside the loop. The rule fires on repeated use of the same cbuffer field or constant-folded cbuffer expression within a loop (e.g., `Sigma * Sigma`, `NearZ`, `FarZ - NearZ`) where the field itself is not indexed by the loop counter. It does not fire when the cbuffer read is through a loop-counter-dependent index (e.g., `LightArray[i].position`), or when the field's value changes inside the loop body (which cannot happen with cbuffer reads, but may happen with local variable aliasing).

See the [What it detects](../rules/cbuffer-load-in-loop.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cbuffer-load-in-loop.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cbuffer-load-in-loop.md -> Examples](../rules/cbuffer-load-in-loop.md#examples).

## See also

- [Rule page](../rules/cbuffer-load-in-loop.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
