---
title: "dot-on-axis-aligned-vector: Calls to `dot(v, c)` (or the symmetric `dot(c, v)`) where `c` is a vector…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: dot-on-axis-aligned-vector
---

# dot-on-axis-aligned-vector

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/dot-on-axis-aligned-vector.md](../rules/dot-on-axis-aligned-vector.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`dot` is a high-level intrinsic that lowers to a multiply-and-reduce sequence on every GPU. For a `float3`, `dot(v, c)` lowers to roughly one multiply per component (three FP32 multiplies into a vector temporary) and a two-step horizontal add (two more FP32 adds reducing to a scalar), or, when the compiler can fold the multiplies into MADs, two `v_fma_f32` instructions and one `v_add_f32` on AMD RDNA 3. NVIDIA Ada Lovelace `FFMA`/`FADD` lowering is structurally identical. Even with optimal MAD folding, that is three to five issued VALU instructions for what, when one of the operands is `(1, 0, 0)`, is mathematically just `v.x` — a zero-instruction operation: a swizzle is a free register-name remapping that emits no machine code at all on AMD or NVIDIA hardware (and on Intel Xe-HPG, swizzles are encoded as source-operand modifiers on the consuming instruction, similarly free).

## What the rule fires on

Calls to `dot(v, c)` (or the symmetric `dot(c, v)`) where `c` is a vector literal whose components are all zero except for one component which is exactly 1.0 or -1.0 — that is, an axis-aligned constant such as `float3(1, 0, 0)`, `float4(0, 0, 1, 0)`, or `float3(0, -1, 0)`. The rule matches both inline-literal forms (`dot(v, float3(1, 0, 0))`) and named-constant forms when the constant's value is visible to the parser as a `static const` initialiser. The match also covers the trivial single-axis selection idioms like `dot(v, float3(1, 0, 0))` for selecting `v.x`. It does not fire when the constant has more than one non-zero component (a true diagonal-axis dot product is a meaningful operation, not a swizzle), and does not fire when the constant is loaded from a constant buffer, structured buffer, or any other runtime source.

See the [What it detects](../rules/dot-on-axis-aligned-vector.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/dot-on-axis-aligned-vector.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[dot-on-axis-aligned-vector.md -> Examples](../rules/dot-on-axis-aligned-vector.md#examples).

## See also

- [Rule page](../rules/dot-on-axis-aligned-vector.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
