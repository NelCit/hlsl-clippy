---
title: "interlocked-float-bit-cast-trick"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: interlocked-float-bit-cast-trick
---

# interlocked-float-bit-cast-trick

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/interlocked-float-bit-cast-trick.md](../rules/interlocked-float-bit-cast-trick.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Atomic min/max on floats is needed in several common workloads: depth-of-field min-Z buffers, soft-shadow blocker depth reductions, voxel-cone tracing min-distance writes, and TAA history-clip range computations. Before SM 6.6, HLSL had no atomic float intrinsic, so shaders cast the float to `uint`, exploited the property that IEEE 754 single-precision floats compare correctly as signed magnitudes when sign-flipped, did the integer atomic, then cast back. The trick is correct, but it expands to roughly 8-12 instructions per atomic call site (sign extraction, conditional XOR, integer atomic, inverse sign restoration, `asfloat`), and requires the shader author to remember the sign convention for their value range â€” a frequent source of subtle off-by-sign bugs in negative-depth or negative-LOD code paths.

## What the rule fires on

The hand-rolled idiom for atomic min/max on floating-point data: a sequence of `asuint(x)`, conditional sign-bit flip (`x ^ 0x80000000` for negatives, `~x` for the inverted-comparison trick), `InterlockedMin` / `InterlockedMax` on the resulting `uint`, and a final `asfloat` to recover the value. The rule fires when the surrounding shader is compiled for SM 6.6 or higher, where `InterlockedMin(float, float)` and `InterlockedMax(float, float)` are native intrinsics and the bit-cast dance is no longer necessary.

See the [What it detects](../rules/interlocked-float-bit-cast-trick.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/interlocked-float-bit-cast-trick.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[interlocked-float-bit-cast-trick.md -> Examples](../rules/interlocked-float-bit-cast-trick.md#examples).

## See also

- [Rule page](../rules/interlocked-float-bit-cast-trick.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
