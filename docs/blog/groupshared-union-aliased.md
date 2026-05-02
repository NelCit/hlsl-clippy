---
title: "groupshared-union-aliased"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-union-aliased
---

# groupshared-union-aliased

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-union-aliased.md](../rules/groupshared-union-aliased.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

LDS (local data store / shared memory) on every desktop GPU is a typeless byte-addressable scratchpad with a small bank-aware optimiser. AMD RDNA 2/3 LDS is 64 KB per WGP organised as 32 banks of 4 bytes; NVIDIA Turing/Ada shared memory is 96 KB or 128 KB per SM in 32 banks of 4 bytes; Intel Xe-HPG SLM is 128 KB per Xe-core in 32 banks. The compiler reasons about LDS access patterns to schedule loads, fold redundant reads, and elide write-then-read traffic when the result can stay in registers. That reasoning relies on a stable type at each offset: when the HLSL source aliases two types across the same cell, the compiler must conservatively assume any write may invalidate any read, regardless of which type is in flight.

## What the rule fires on

A `groupshared` declaration whose layout exposes two distinct typed views over the same byte offset, either via a manual `asuint`/`asfloat` round-trip pattern (writing as one type and reading as another against the same `groupshared` cell) or via a struct-hack that places different-typed fields at the same logical offset. The detector uses Slang reflection's groupshared layout to identify aliased offsets and matches the AST round-trip pattern at the access site. It does not fire on a single-type groupshared array nor on `asuint`/`asfloat` round-trips against locals or buffers (only against `groupshared` storage).

See the [What it detects](../rules/groupshared-union-aliased.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-union-aliased.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-union-aliased.md -> Examples](../rules/groupshared-union-aliased.md#examples).

## See also

- [Rule page](../rules/groupshared-union-aliased.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
