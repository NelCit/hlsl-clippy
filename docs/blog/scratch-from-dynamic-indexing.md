---
title: "scratch-from-dynamic-indexing: A local array declared inside a function body — `float4 lut[N]` or similar —…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: memory
tags: [hlsl, performance, memory]
status: stub
related-rule: scratch-from-dynamic-indexing
---

# scratch-from-dynamic-indexing

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/scratch-from-dynamic-indexing.md](../rules/scratch-from-dynamic-indexing.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On AMD RDNA and NVIDIA Turing-class hardware, the register file is not randomly addressable by lane-computed indices at the instruction level. A register instruction must name its source and destination operands at compile time; there is no "register indirect" addressing mode in the ISA. When the HLSL compiler encounters a local array indexed by a runtime value it cannot eliminate, it maps the array to scratch memory: a per-lane stack allocation that lives in the VRAM L2 or GDDR cache hierarchy, not in the on-chip register file. On RDNA 3, scratch-memory bandwidth to L2 runs at roughly 1.28 TB/s aggregate, but per-thread latency for a random-access scratch read is 80-300 cycles — compared to roughly 4 cycles for a register read. A shader that touches a scratch array in its inner loop will stall waiting for off-chip data even though the array is logically small.

## What the rule fires on

A local array declared inside a function body — `float4 lut[N]` or similar — that is indexed with a non-constant expression: a cbuffer field, an `SV_GroupIndex`, or any value whose value is unknown at compile time. The rule fires when the compiler cannot statically determine which array element is accessed, forcing the array to be allocated as an indexable temporary (scratch memory) rather than being scalarised into individual registers.

See the [What it detects](../rules/scratch-from-dynamic-indexing.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/scratch-from-dynamic-indexing.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[scratch-from-dynamic-indexing.md -> Examples](../rules/scratch-from-dynamic-indexing.md#examples).

## See also

- [Rule page](../rules/scratch-from-dynamic-indexing.md) -- canonical reference + change log.
- [memory overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
