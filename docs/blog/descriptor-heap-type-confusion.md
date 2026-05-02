---
title: "descriptor-heap-type-confusion"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: descriptor-heap-type-confusion
---

# descriptor-heap-type-confusion

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/descriptor-heap-type-confusion.md](../rules/descriptor-heap-type-confusion.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12 descriptor heaps come in two distinct physical types: `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` (for constant buffer views, shader resource views, and unordered access views) and `D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER` (for samplers). These are separate memory regions in driver-managed GPU-accessible memory. At the hardware level â€” RDNA, Turing, Xe-HPG â€” the descriptor table pointer that the driver uses to locate a sampler lives in a different register from the one that locates CBVs/SRVs/UAVs. When HLSL accesses `SamplerDescriptorHeap[i]` and casts the result to a `Texture2D`, the driver forwards the sampler-heap base address to a resource-descriptor load unit; that unit interprets sampler-heap binary data as a resource descriptor. The result is a garbage resource handle.

## What the rule fires on

An SM 6.6 dynamic heap access where the descriptor heap type does not match the resource type being requested: a sampler resource fetched from `ResourceDescriptorHeap` (the CBV/SRV/UAV heap), or a non-sampler resource (texture, buffer, CBV) fetched from `SamplerDescriptorHeap`. The rule uses Slang's type reflection to determine the declared type of the variable receiving the heap access (e.g., `SamplerState`, `SamplerComparisonState` vs `Texture2D`, `Buffer<>`, `ConstantBuffer<>`) and compares it against which heap object is used. Both `ResourceDescriptorHeap` and `SamplerDescriptorHeap` are accessible from the same shader source, but they index into physically separate heap arrays; reading across the boundary is undefined behaviour per the D3D12 specification.

See the [What it detects](../rules/descriptor-heap-type-confusion.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/descriptor-heap-type-confusion.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[descriptor-heap-type-confusion.md -> Examples](../rules/descriptor-heap-type-confusion.md#examples).

## See also

- [Rule page](../rules/descriptor-heap-type-confusion.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
