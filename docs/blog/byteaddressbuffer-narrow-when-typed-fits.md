---
title: "byteaddressbuffer-narrow-when-typed-fits"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: byteaddressbuffer-narrow-when-typed-fits
---

# byteaddressbuffer-narrow-when-typed-fits

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/byteaddressbuffer-narrow-when-typed-fits.md](../rules/byteaddressbuffer-narrow-when-typed-fits.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`ByteAddressBuffer` and typed buffer views travel through different cache paths on every modern desktop IHV. On AMD RDNA 2/3, `ByteAddressBuffer` accesses go through the K$/scalar L1 path because the byte addressing exposes raw memory; `Buffer<float4>` and `StructuredBuffer<T>` are bound through the texture descriptor path, which routes through the V$/texture L1 with format-aware widening hardware. On NVIDIA Turing and Ada Lovelace, typed loads use the texture/L1 unified cache with format converters in the load pipeline, while raw byte loads go through the LD/ST units against the generic L1. Intel Xe-HPG draws a similar distinction between the typed sampler/L1 path and the URB-style raw load.

## What the rule fires on

A `ByteAddressBuffer.Load4` (or `Load2` / `Load3`) followed by an `asfloat` / `asuint` round-trip that yields a POD whose size and layout exactly match a `Buffer<float4>`, `Buffer<uint4>`, or `StructuredBuffer<T>` view available against the same resource. The detector uses Slang reflection to recognise the binding's underlying resource and matches the local POD shape produced by the round-trip against typed-view candidates with identical stride. It does not fire when the bytes are reinterpreted in a way that no typed view supports (e.g. mixed `uint16_t` and `uint32_t` lanes within the same load).

See the [What it detects](../rules/byteaddressbuffer-narrow-when-typed-fits.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/byteaddressbuffer-narrow-when-typed-fits.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[byteaddressbuffer-narrow-when-typed-fits.md -> Examples](../rules/byteaddressbuffer-narrow-when-typed-fits.md#examples).

## See also

- [Rule page](../rules/byteaddressbuffer-narrow-when-typed-fits.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
