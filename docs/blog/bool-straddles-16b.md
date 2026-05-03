---
title: "bool-straddles-16b"
date: 2026-05-02
author: shader-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: bool-straddles-16b
---

# bool-straddles-16b

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/bool-straddles-16b.md](../rules/bool-straddles-16b.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A `bool` in a cbuffer is loaded as part of a 16-byte slot fetch. When the `bool` sits at bytes 12â€“15 of a slot, the load is well-defined and correct â€” the problem is that the D3D12 / HLSL specification treats the effective value as the result of a non-zero test on the underlying DWORD, but the exact byte layout of that DWORD relative to the 16-byte register depends on whether the compiler chose to pack it into the same slot or bumped it to the next one. `fxc` and `dxc` have historically made different decisions for this layout, meaning a cbuffer filled on the CPU side using one compiler's reflection output may be read incorrectly by a shader compiled with another. This is a correctness hazard, not merely a performance issue.

## What the rule fires on

A `bool` member inside a `cbuffer` or `ConstantBuffer<T>` whose byte offset causes it to span â€” or be placed at â€” a 16-byte register boundary. Under HLSL packing rules a `bool` occupies 4 bytes (one DWORD, promoted from 1-bit to 32-bit for GPU register layout), but the packing rule that prevents members from straddling a 16-byte slot boundary can place the `bool` at byte offset 12 inside a slot, leaving it exactly at the boundary. The rule uses Slang's reflection API to retrieve each member's byte offset and size, then checks whether `(offset % 16) + sizeof(member) > 16`. The canonical trigger is `float3` (12 bytes) followed by `bool`: `float3` lands at an aligned offset, consumes 12 bytes, and the `bool` at offset 12 within the slot technically fits in the last 4 bytes â€” but whether the compiler packs it there or bumps it to the next slot is implementation-defined and varies across `dxc`, `fxc`, and Slang. See `tests/fixtures/phase3/bindings.hlsl`, lines 12â€“18 for the `StraddleCB` example.

See the [What it detects](../rules/bool-straddles-16b.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/bool-straddles-16b.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[bool-straddles-16b.md -> Examples](../rules/bool-straddles-16b.md#examples).

## See also

- [Rule page](../rules/bool-straddles-16b.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
