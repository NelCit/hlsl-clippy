---
title: "non-uniform-resource-index"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: non-uniform-resource-index
---

# non-uniform-resource-index

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/non-uniform-resource-index.md](../rules/non-uniform-resource-index.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The DXIL specification (and by extension the HLSL specification for SM 5.1 and later) defines it as undefined behaviour to index a resource array with a non-uniform value without the `NonUniformResourceIndex` marker. The reason is architectural: resource descriptors are resolved by the driver before shader dispatch, and the hardware uses a single descriptor index per wave to look up the resource binding. If all lanes in a wave use the same index (uniform access), the driver emits a single descriptor load and broadcasts the resource handle to all lanes. If the index varies per lane (non-uniform), the driver must emit a waterfall loop â€” a sequential iteration over unique index values, masking inactive lanes at each step.

## What the rule fires on

A dynamic index into a resource array parameter â€” such as `Texture2D textures[]`, `ConstantBuffer<T> cbs[]`, or any other unbounded / bounded resource array â€” where the index is a per-lane divergent value and is not wrapped in `NonUniformResourceIndex(...)`. The rule uses Slang's reflection API to identify parameters of array-of-resource type, then uses Slang's uniformity analysis to determine whether each index expression could differ across lanes in the same wave. It does not fire when the index is a compile-time constant, a root constant, or any value that Slang can prove is wave-uniform.

See the [What it detects](../rules/non-uniform-resource-index.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/non-uniform-resource-index.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[non-uniform-resource-index.md -> Examples](../rules/non-uniform-resource-index.md#examples).

## See also

- [Rule page](../rules/non-uniform-resource-index.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
