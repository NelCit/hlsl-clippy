---
title: "vgpr-pressure-warning"
date: 2026-05-02
author: hlsl-clippy maintainers
category: memory
tags: [hlsl, performance, memory]
status: stub
related-rule: vgpr-pressure-warning
---

# vgpr-pressure-warning

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/vgpr-pressure-warning.md](../rules/vgpr-pressure-warning.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Every VGPR (vector general-purpose register) allocated to a shader is multiplied across every lane in a wave. On AMD RDNA 2/3, a wave is 32 or 64 lanes wide, and the hardware VGPR file holds 1536 registers per compute unit per SIMD32 unit. If a shader requires 80 VGPRs per lane, only 1536/80 = 19 waves can be resident concurrently per SIMD32 â€” barely two full waves. At 64 VGPRs the number rises to 24 waves; at 32 VGPRs it reaches the hardware maximum of 48 waves. Fewer resident waves means the scheduler has fewer instruction streams to overlap with memory latency, and the arithmetic units stall waiting for texture or buffer returns. The effect is especially pronounced in pixel shaders with many simultaneous texture fetches, where latency-hiding determines whether the TMU operates at peak throughput.

## What the rule fires on

A live-range-based static estimate of per-lane VGPR consumption for each entry-point function in the compiled DXIL. The rule fires when the estimated peak live register count exceeds a configurable per-stage threshold. The estimate counts simultaneously live scalar and vector values after register allocation; it correlates with, but does not exactly reproduce, what the compiler back-end reports. The rule fires at the function boundary and identifies the source region (line range in the original HLSL) that drives the peak.

See the [What it detects](../rules/vgpr-pressure-warning.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/vgpr-pressure-warning.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[vgpr-pressure-warning.md -> Examples](../rules/vgpr-pressure-warning.md#examples).

## See also

- [Rule page](../rules/vgpr-pressure-warning.md) -- canonical reference + change log.
- [memory overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
