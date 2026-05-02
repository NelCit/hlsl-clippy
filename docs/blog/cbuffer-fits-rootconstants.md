---
title: "cbuffer-fits-rootconstants: A `cbuffer` or `ConstantBuffer<T>` whose total size is at most 32 bytes (8 DWORDs)…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: cbuffer-fits-rootconstants
---

# cbuffer-fits-rootconstants

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cbuffer-fits-rootconstants.md](../rules/cbuffer-fits-rootconstants.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

On D3D12, a cbuffer bound via a descriptor heap or root descriptor requires an indirection: the GPU reads a pointer from the root signature, fetches the constant-buffer view descriptor, and then loads the cbuffer data from the address the descriptor names. This is two dependent memory reads before the first shader data arrives. Root constants, by contrast, live directly in the command list's root signature data: the driver uploads them into the root-argument area at `ExecuteCommandLists` time, and the GPU reads them from a small root-argument register block that is broadcast to all invocations of the dispatch or draw without any descriptor-table indirection.

## What the rule fires on

A `cbuffer` or `ConstantBuffer<T>` whose total size is at most 32 bytes (8 DWORDs) — the maximum number of 32-bit values that D3D12 root constants can hold in one root parameter slot. The rule uses Slang's reflection API to determine the cbuffer's total byte size and fires when `total_bytes <= 32`. Both fixture examples qualify: `cbuffer Tiny` (8 bytes, 2 DWORDs; see `tests/fixtures/phase3/bindings.hlsl`, lines 21–24) and the four-DWORD `cbuffer PushCB` and the two-DWORD `cbuffer TinyBlurCB` (see `tests/fixtures/phase3/bindings_extra.hlsl`, lines 8–19).

See the [What it detects](../rules/cbuffer-fits-rootconstants.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cbuffer-fits-rootconstants.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cbuffer-fits-rootconstants.md -> Examples](../rules/cbuffer-fits-rootconstants.md#examples).

## See also

- [Rule page](../rules/cbuffer-fits-rootconstants.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
