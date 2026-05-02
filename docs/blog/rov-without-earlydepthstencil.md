---
title: "rov-without-earlydepthstencil"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: rov-without-earlydepthstencil
---

# rov-without-earlydepthstencil

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/rov-without-earlydepthstencil.md](../rules/rov-without-earlydepthstencil.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

ROVs enforce ordering between pixel shader invocations that cover the same pixel: later-rasterized primitives must not commit their ROV writes until earlier-rasterized primitives have completed. On AMD RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace, this ordering is implemented via a serialization primitive â€” typically a per-pixel lock or a per-pixel write-order fence â€” that the hardware acquires before any ROV access and releases after. The critical section is the span of shader code between the lock acquisition and the lock release.

## What the rule fires on

A pixel shader entry point that declares one or more `RasterizerOrderedBuffer<T>`, `RasterizerOrderedTexture2D<T>`, or any other `RasterizerOrdered*` resource (ROV) without the `[earlydepthstencil]` function attribute, and without a `discard` statement or a `SV_Depth` write that would make early depth legally ambiguous. The rule uses Slang's reflection API to identify entry points with ROV-typed resource bindings and checks whether the `[earlydepthstencil]` attribute is present. It does not fire when the shader contains `discard`, writes `SV_Depth`, or writes `SV_Coverage` â€” situations where `[earlydepthstencil]` would change semantics.

See the [What it detects](../rules/rov-without-earlydepthstencil.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/rov-without-earlydepthstencil.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[rov-without-earlydepthstencil.md -> Examples](../rules/rov-without-earlydepthstencil.md#examples).

## See also

- [Rule page](../rules/rov-without-earlydepthstencil.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
