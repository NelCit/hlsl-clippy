---
title: "rwresource-read-only-usage"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: rwresource-read-only-usage
---

# rwresource-read-only-usage

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/rwresource-read-only-usage.md](../rules/rwresource-read-only-usage.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

UAV (unordered access view) resources and SRV (shader resource view) resources differ in how the GPU's memory subsystem handles them. An SRV is read-only at the hardware level: the GPU can apply aggressive caching, prefetching, and compression (e.g., delta colour compression on texture SRVs on AMD RDNA) because the hardware knows no write will invalidate cached data during the current dispatch. A UAV is read-write: the GPU must treat each access as potentially invalidating the cache, which disables or limits these optimisations.

## What the rule fires on

An `RWBuffer<T>`, `RWStructuredBuffer<T>`, `RWTexture1D<T>`, `RWTexture2D<T>`, `RWTexture3D<T>`, or any other read-write (UAV) resource declaration that is only ever read in the shader â€” never written to and never passed to an intrinsic that performs a write (e.g., `InterlockedAdd`, `Store`, assignment via `[]` operator). The rule uses Slang's reflection API to identify UAV-typed resources and checks all access sites for write operations. See `tests/fixtures/phase3/bindings.hlsl`, lines 48â€“53 (`ReadOnlyRW`, accessed only as `ReadOnlyRW[0]` on the right-hand side) and `tests/fixtures/phase3/bindings_extra.hlsl`, lines 50â€“55 (`AccumBuffer`, accessed only via `AccumBuffer.Load(...)`).

See the [What it detects](../rules/rwresource-read-only-usage.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/rwresource-read-only-usage.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[rwresource-read-only-usage.md -> Examples](../rules/rwresource-read-only-usage.md#examples).

## See also

- [Rule page](../rules/rwresource-read-only-usage.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
