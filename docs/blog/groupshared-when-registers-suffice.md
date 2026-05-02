---
title: "groupshared-when-registers-suffice: A `groupshared` array used as per-thread scratch — written by thread `tid` and read…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-when-registers-suffice
---

# groupshared-when-registers-suffice

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-when-registers-suffice.md](../rules/groupshared-when-registers-suffice.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

LDS / shared-memory is a precious resource on every modern GPU. AMD RDNA 2/3 has 64 KB of LDS per CU shared across all in-flight workgroups — a kernel that allocates 8 KB of LDS per workgroup limits in-flight workgroups to 8 per CU, which directly caps the kernel's wave occupancy and its ability to hide memory latency. NVIDIA Turing/Ada Lovelace has 100 KB of shared memory per SM with similar trade-offs. Intel Xe-HPG has 128 KB of SLM per Xe core. The marginal occupancy cliff is steep — going from 8 KB to 4 KB per workgroup typically doubles in-flight workgroups, doubles latency hiding, and can deliver 1.5-3x perf on memory-bound kernels.

## What the rule fires on

A `groupshared` array used as per-thread scratch — written by thread `tid` and read only by thread `tid` (no cross-thread access) — whose size per thread is small enough that the compiler could keep it in registers. Default threshold: 8 elements per thread. The Phase 7 IR-level register-pressure analysis verifies that promoting the array to private (per-thread) registers does not push the kernel over the per-CU/SM register budget.

See the [What it detects](../rules/groupshared-when-registers-suffice.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-when-registers-suffice.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-when-registers-suffice.md -> Examples](../rules/groupshared-when-registers-suffice.md#examples).

## See also

- [Rule page](../rules/groupshared-when-registers-suffice.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
