---
title: "cbuffer-large-fits-rootcbv-not-table: A `cbuffer` (or `ConstantBuffer<T>`) whose total size, as reported by Slang reflection, fits within…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: cbuffer-large-fits-rootcbv-not-table
---

# cbuffer-large-fits-rootcbv-not-table

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cbuffer-large-fits-rootcbv-not-table.md](../rules/cbuffer-large-fits-rootcbv-not-table.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12's root signature distinguishes three binding kinds for cbuffers, in increasing indirection: root constants (inline DWORDs in the root signature, no memory load), root CBVs (a 64-bit GPU virtual address inline in the root signature, one memory load to fetch the cbuffer data), and descriptor-table CBVs (a heap offset in the root signature, one memory load to fetch the descriptor + one to fetch the cbuffer data). The descriptor-table path costs an extra memory dereference per cbuffer access compared to the root-CBV path.

## What the rule fires on

A `cbuffer` (or `ConstantBuffer<T>`) whose total size, as reported by Slang reflection, fits within the D3D12 root CBV size limit (a CBV may address up to 65536 bytes per the D3D12 spec) and that is referenced once per dispatch / draw — i.e. the binding does not require an array of CBVs swept by an index — but is currently bound through a descriptor table rather than as a root CBV. The detector reads the binding kind from reflection (descriptor table vs root CBV vs root constants) and matches against the cbuffer size and access pattern. **D3D12-relevant:** Vulkan binds uniform buffers via descriptor sets without the root-vs-table distinction (push descriptors are the closest equivalent), and Metal handles small constant buffers via setBytes/setBuffer on the encoder; this rule still surfaces a portability concern because the descriptor-indirection cost shows up as extra indirect loads on every backend, even when the API surface differs.

See the [What it detects](../rules/cbuffer-large-fits-rootcbv-not-table.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cbuffer-large-fits-rootcbv-not-table.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cbuffer-large-fits-rootcbv-not-table.md -> Examples](../rules/cbuffer-large-fits-rootcbv-not-table.md#examples).

## See also

- [Rule page](../rules/cbuffer-large-fits-rootcbv-not-table.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
