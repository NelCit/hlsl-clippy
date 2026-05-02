---
title: "manual-f32tof16"
date: 2026-05-02
author: hlsl-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: manual-f32tof16
---

# manual-f32tof16

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/manual-f32tof16.md](../rules/manual-f32tof16.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`f32tof16` and `f16tof32` are single-instruction operations on all modern GPU targets. On AMD RDNA they map to `v_cvt_f16_f32` and `v_cvt_f32_f16`. On NVIDIA Turing they map to the `F2FP` / `HADD2` conversion path. Each issues in a single ALU cycle. A hand-rolled implementation performs 8-12 integer operations: a bitcast, two or three shifts, two or three masks, an arithmetic operation on the exponent, and a final bitcast back. Even with full pipelining, that sequence is 8-12 cycles of ALU throughput versus 1. In a compute shader that packs thousands of FP16 values into a UAV buffer, the difference is measurable as a fraction of the total dispatch time.

## What the rule fires on

Hand-written bit-twiddling sequences that implement an FP32-to-FP16 or FP16-to-FP32 conversion manually using `asuint`, `asfloat`, bit-shifts, masks, and bias additions, rather than calling the `f32tof16` / `f16tof32` intrinsics (available since SM 5.0) or using a `min16float` cast. The canonical bad patterns are: extracting the sign bit with `(x >> 31) & 1`, extracting the exponent with `(x >> 23) & 0xFF`, re-biasing by subtracting 127 and adding 15, masking the mantissa, and assembling the result â€” all performed on the raw `uint` bitcast of the `float`. The rule matches both the full conversion and common sub-idioms that partially re-implement the intrinsic.

See the [What it detects](../rules/manual-f32tof16.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/manual-f32tof16.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[manual-f32tof16.md -> Examples](../rules/manual-f32tof16.md#examples).

## See also

- [Rule page](../rules/manual-f32tof16.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
