---
title: "min16float-in-cbuffer-roundtrip"
date: 2026-05-02
author: shader-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: min16float-in-cbuffer-roundtrip
---

# min16float-in-cbuffer-roundtrip

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/min16float-in-cbuffer-roundtrip.md](../rules/min16float-in-cbuffer-roundtrip.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`cbuffer` (constant buffer) fields are always stored as 32-bit aligned types on the GPU. When a shader reads a `float` cbuffer field and casts it to `min16float`, the compiler emits a `v_cvt_f16_f32` (RDNA) or `F2FP` (Turing) conversion instruction on every execution of that load. For a pixel shader invoked millions of times per frame â€” or a compute shader across thousands of thread groups â€” this single conversion instruction is replicated across every wave. On RDNA 3, `v_cvt_f16_f32` costs one VALU cycle, which is not itself expensive, but when the field is accessed inside a loop the instruction is issued once per iteration per wave, and the ALU time accumulates.

## What the rule fires on

A `min16float` (or `half`) cast applied to a `float` field loaded from a `cbuffer`. The cbuffer field is declared as 32-bit `float`; the cast expression `(min16float)CbField` or `min16float(CbField)` performs a 32-to-16 demotion on every read. The rule fires when this pattern appears in a function body that is called repeatedly (in a loop, in a pixel shader, or in a compute shader hot path), because the 32-to-16 conversion is paid on every invocation rather than being absorbed into a one-time constant promotion.

See the [What it detects](../rules/min16float-in-cbuffer-roundtrip.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/min16float-in-cbuffer-roundtrip.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[min16float-in-cbuffer-roundtrip.md -> Examples](../rules/min16float-in-cbuffer-roundtrip.md#examples).

## See also

- [Rule page](../rules/min16float-in-cbuffer-roundtrip.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
