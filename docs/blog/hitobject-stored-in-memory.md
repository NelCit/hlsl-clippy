---
title: "hitobject-stored-in-memory"
date: 2026-05-02
author: shader-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: hitobject-stored-in-memory
---

# hitobject-stored-in-memory

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/hitobject-stored-in-memory.md](../rules/hitobject-stored-in-memory.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`dx::HitObject` represents a deferred ray-tracing hit that has already executed traversal but has not yet been dispatched to its closest-hit / any-hit / miss shader. On NVIDIA Ada Lovelace (the launch IHV for SER) the HitObject lives in a per-lane register file slice that the RT cores own jointly with the SM; on AMD RDNA 3/4 (when SER ships there) the same lifetime constraint applies. Storing a HitObject in memory is meaningless because the runtime cannot reconstruct the per-lane RT-core state from a flat byte representation â€” there is no canonical layout, and the spec deliberately leaves it implementation-defined.

## What the rule fires on

A `dx::HitObject` value (the SM 6.9 Shader Execution Reordering type) stored into any memory location: a `groupshared` declaration, a UAV write (`StructuredBuffer<dx::HitObject>` or `RWByteAddressBuffer.Store`), a return slot of a non-inlined function, or a globally-scoped variable. The SER specification (HLSL proposal 0027) restricts `dx::HitObject` to register-only lifetimes inside an inlined call chain rooted at a raygeneration shader. Reflection-aware analysis identifies the type via Slang's HLSL frontend and walks the assignment / return / store sites.

See the [What it detects](../rules/hitobject-stored-in-memory.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/hitobject-stored-in-memory.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[hitobject-stored-in-memory.md -> Examples](../rules/hitobject-stored-in-memory.md#examples).

## See also

- [Rule page](../rules/hitobject-stored-in-memory.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
