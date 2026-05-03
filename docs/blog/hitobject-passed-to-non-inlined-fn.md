---
title: "hitobject-passed-to-non-inlined-fn"
date: 2026-05-02
author: shader-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: hitobject-passed-to-non-inlined-fn
---

# hitobject-passed-to-non-inlined-fn

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/hitobject-passed-to-non-inlined-fn.md](../rules/hitobject-passed-to-non-inlined-fn.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The SER programming model bakes inlining into its hardware contract: the HitObject's per-lane state lives in registers that the RT cores own jointly with the SM, and the runtime only knows how to materialise / dematerialise that state at well-defined boundaries (raygen entry, `Invoke`, `MaybeReorderThread`). When a HitObject crosses a non-inlined call boundary, the calling convention has no recipe for spilling it â€” the spec marks the operation undefined precisely because no IHV has a defined hardware path for the spill. NVIDIA Ada Lovelace's compiler emits a hard error for the simplest forms; future implementations may either emit garbage or fault.

## What the rule fires on

A `dx::HitObject` value passed as an argument to or returned from a function that the call-graph analysis cannot prove is inlined into its raygen caller. The SM 6.9 SER specification requires HitObject lifetimes to stay inside an inlined call chain; a non-inlined function boundary forces the runtime to spill the HitObject across the call, which is undefined behaviour. The Phase 4 inter-procedural analysis tracks `[noinline]` annotations, recursive calls, and indirect calls (`function pointers`) that block inlining.

See the [What it detects](../rules/hitobject-passed-to-non-inlined-fn.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/hitobject-passed-to-non-inlined-fn.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[hitobject-passed-to-non-inlined-fn.md -> Examples](../rules/hitobject-passed-to-non-inlined-fn.md#examples).

## See also

- [Rule page](../rules/hitobject-passed-to-non-inlined-fn.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
