---
title: "groupshared-too-large"
date: 2026-05-02
author: shader-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-too-large
---

# groupshared-too-large

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-too-large.md](../rules/groupshared-too-large.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Groupshared memory maps directly to Local Data Store (LDS) on AMD hardware and Shared Memory on NVIDIA hardware. Both are on-die SRAM resources that are physically partitioned among the compute units (CUs) or streaming multiprocessors (SMs) running concurrently on the chip. On AMD RDNA 3, each CU has 64 KB of LDS. On NVIDIA Turing, each SM has 64 KB of configurable shared memory (the split between L1 cache and shared memory is chosen at kernel launch, with a maximum shared memory allocation of 48 KB per thread block under the default split). On Intel Xe-HPG, each Xe core has 64 KB of shared local memory per EU cluster.

## What the rule fires on

One or more `groupshared` variable declarations in a compute shader whose combined byte size across the shader's compilation unit exceeds the configured `threshold-bytes`. The default threshold is 16384 bytes (16 KB). The size is computed from the declared types: `groupshared float HugeShared[16384]` is 65536 bytes (64 KB) and fires the rule at the default threshold. Arrays of `float4` (16 bytes each) and matrices are counted accordingly. All `groupshared` declarations visible to the compilation unit are summed, not counted per-function. The rule reports the total computed size alongside the threshold. It does not fire when the total is at or below the threshold.

See the [What it detects](../rules/groupshared-too-large.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-too-large.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-too-large.md -> Examples](../rules/groupshared-too-large.md#examples).

## See also

- [Rule page](../rules/groupshared-too-large.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
