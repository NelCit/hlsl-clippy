---
title: "oversized-cbuffer"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: oversized-cbuffer
---

# oversized-cbuffer

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/oversized-cbuffer.md](../rules/oversized-cbuffer.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Every cbuffer bound to a shader occupies space in the GPU's constant-data path. On AMD RDNA/RDNA 2/RDNA 3, constant data flows through the scalar register file (SGPRs) and the associated scalar data cache (K-cache, 16 KB per CU shared across all waves on that CU). On NVIDIA Turing and Ada Lovelace, cbuffer contents live in a dedicated constant-data L1 cache. In both cases, a large cbuffer that exceeds the effective cache capacity forces evictions: data loaded by one wave is evicted before adjacent waves can reuse it, turning what should be a broadcast-per-CU operation into repeated cache-miss fetches from L2 or VRAM.

## What the rule fires on

A `cbuffer` or `ConstantBuffer<T>` whose total byte size, as reported by Slang's reflection API, exceeds a configurable threshold (default: 4096 bytes, i.e., 4 KB). The rule fires on the declaration itself, naming the actual size and the threshold. The canonical trigger in the fixture is `cbuffer Huge` with a `float4[256]` member (4096 bytes) plus a `float4 Tail` (16 bytes), totalling 4112 bytes (see `tests/fixtures/phase3/bindings.hlsl`, lines 27â€“31).

See the [What it detects](../rules/oversized-cbuffer.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/oversized-cbuffer.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[oversized-cbuffer.md -> Examples](../rules/oversized-cbuffer.md#examples).

## See also

- [Rule page](../rules/oversized-cbuffer.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
