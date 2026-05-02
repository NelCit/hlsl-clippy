---
title: "long-vector-in-cbuffer-or-signature: A `vector<T, N>` with `N >= 5` declared as a member of a `cbuffer`…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: long-vectors
tags: [hlsl, performance, long-vectors]
status: stub
related-rule: long-vector-in-cbuffer-or-signature
---

# long-vector-in-cbuffer-or-signature

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/long-vector-in-cbuffer-or-signature.md](../rules/long-vector-in-cbuffer-or-signature.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The cbuffer packing rules and the inter-stage IO packing rules predate the long-vector feature. Cbuffer layout is fixed by HLSL packing (16-byte slots, `float4`-shaped vector members, scalar tail packing), and the runtime maps cbuffer fetches onto the IHV-specific scalar / constant-data path. Inter-stage IO is packed into per-vertex slots (NVIDIA Ada: 32 four-component slots; AMD RDNA 2/3: parameter-cache entries; Intel Xe-HPG: URB-style slots); the slot allocation is fixed at PSO compile time and assumes 1/2/3/4-wide vector types only.

## What the rule fires on

A `vector<T, N>` with `N >= 5` declared as a member of a `cbuffer` / `ConstantBuffer<T>`, or as the type of a vertex / pixel / hull / domain / geometry IO signature element. The SM 6.9 long-vector specification (DXIL vectors, proposal 0030) restricts long vectors to in-shader compute use; cbuffer layout and stage-IO signatures are explicitly out of scope. Slang reflection identifies the binding kind and signature stage; the rule fires on the first long-vector member encountered at one of those boundaries.

See the [What it detects](../rules/long-vector-in-cbuffer-or-signature.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/long-vector-in-cbuffer-or-signature.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[long-vector-in-cbuffer-or-signature.md -> Examples](../rules/long-vector-in-cbuffer-or-signature.md#examples).

## See also

- [Rule page](../rules/long-vector-in-cbuffer-or-signature.md) -- canonical reference + change log.
- [long-vectors overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
