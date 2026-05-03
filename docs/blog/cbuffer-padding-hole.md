---
title: "cbuffer-padding-hole"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: cbuffer-padding-hole
---

# cbuffer-padding-hole

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cbuffer-padding-hole.md](../rules/cbuffer-padding-hole.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

cbuffer reads on all current GPU hardware â€” AMD RDNA/RDNA 2/RDNA 3 and NVIDIA Turing/Ada Lovelace â€” travel through the scalar/constant-data path. The hardware delivers constant data in 16-byte register slots (four `float` registers, one `float4`). A single cbuffer fetch retrieves one or more complete 128-bit slots; there is no mechanism to fetch a sub-register fraction. When padding holes exist, each 16-byte slot that contains a hole contains less usable data than it could: the padding bytes are transmitted across the constant-bus, occupy space in the L1 constant cache, and are then discarded by the shader.

## What the rule fires on

A `cbuffer` or `ConstantBuffer<T>` declaration whose member layout contains one or more implicit padding holes: gaps of unused bytes inserted by the HLSL packing rules (HLSL pack rule: each member is aligned to the smaller of its own size or 16 bytes, and no member may straddle a 16-byte boundary). The rule uses Slang's reflection API to retrieve the byte offset of every member, computes the gaps between consecutive members, and fires when any gap is non-zero. Common examples: `float Time` (4 bytes) followed by `float3 LightDir` (12 bytes, 16-byte aligned) leaves a 12-byte hole at offsets 4â€“15 (see `tests/fixtures/phase3/bindings.hlsl`, line 3â€“10).

See the [What it detects](../rules/cbuffer-padding-hole.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cbuffer-padding-hole.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cbuffer-padding-hole.md -> Examples](../rules/cbuffer-padding-hole.md#examples).

## See also

- [Rule page](../rules/cbuffer-padding-hole.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
