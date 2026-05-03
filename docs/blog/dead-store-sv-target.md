---
title: "dead-store-sv-target"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: dead-store-sv-target
---

# dead-store-sv-target

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/dead-store-sv-target.md](../rules/dead-store-sv-target.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A dead store to an `SV_Target` output variable computes and writes a value that the GPU will never read. The computation of the dead value â€” any arithmetic, texture samples, or other operations that produced it â€” is wasted ALU and memory bandwidth. On AMD RDNA and NVIDIA Turing, the compiler may eliminate simple constant dead stores (e.g., `result = float4(1,0,0,1)` followed by an unconditional override), but it cannot in general eliminate dead stores whose right-hand side involves function calls, texture samples, or control-flow-dependent values without a full inter-procedural dead-code elimination pass â€” which GPU shader compilers rarely perform at the full-program scale.

## What the rule fires on

An assignment to a pixel-shader output variable carrying the `SV_Target` semantic (or to a local variable that is returned as `SV_Target`) where that assignment is immediately overwritten on all paths before the value is returned or used. The first write is a dead store: it is never read, so the computation and the write are wasted. The rule uses Slang's reflection API to identify `SV_Target`-typed outputs and a light data-flow pass to detect write-before-read patterns within the same function scope. See `tests/fixtures/phase3/bindings_extra.hlsl`, lines 42â€“46 for the `ps_dead_store` example: `float4 result = float4(1,0,0,1)` is written and then immediately overwritten by `result = float4(0,1,0,1)` before `result` is used.

See the [What it detects](../rules/dead-store-sv-target.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/dead-store-sv-target.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[dead-store-sv-target.md -> Examples](../rules/dead-store-sv-target.md#examples).

## See also

- [Rule page](../rules/dead-store-sv-target.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
