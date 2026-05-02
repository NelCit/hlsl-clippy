---
title: "byteaddressbuffer-load-misaligned: A `Load2`, `Load3`, or `Load4` call on a `ByteAddressBuffer` (or `RWByteAddressBuffer`) where the byte…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: byteaddressbuffer-load-misaligned
---

# byteaddressbuffer-load-misaligned

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/byteaddressbuffer-load-misaligned.md](../rules/byteaddressbuffer-load-misaligned.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`ByteAddressBuffer` widened loads compile to a single `BUFFER_LOAD_DWORDX{2,3,4}` (RDNA) or `LDG.E.{64,128}` (NVIDIA Turing/Ada) memory instruction. The hardware paths assume the address is naturally aligned to the load width: 8 bytes for an `x2`, 16 bytes for `x4`. AMD RDNA 2/3 documents that misaligned vector loads are *split* by the memory pipeline into the corresponding number of single-DWORD transactions — a `Load4` at offset 13 turns into four serial 4-byte loads instead of one 16-byte load. NVIDIA Ada Lovelace's L1 likewise penalises sub-line-aligned widened loads by replaying the access; Intel Xe-HPG's URB load path documents an equivalent fallback.

## What the rule fires on

A `Load2`, `Load3`, or `Load4` call on a `ByteAddressBuffer` (or `RWByteAddressBuffer`) where the byte offset argument is a compile-time constant that does not satisfy the natural-alignment rule for the widened load: 8-byte alignment for `Load2`, 4-byte alignment for the underlying DWORDs but typically 16-byte alignment for `Load3`/`Load4` to land in a single cache line. The detector folds constant-arithmetic offsets (literal + literal, `K * sizeof(uint)` patterns) and also fires on the obvious `buf.Load4(13)`, `buf.Load2(6)`, and `buf.Load4(4 + 1)` shapes. It does not fire on offsets backed by a runtime variable; that case is reserved for a Phase 4 uniformity-aware follow-up.

See the [What it detects](../rules/byteaddressbuffer-load-misaligned.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/byteaddressbuffer-load-misaligned.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[byteaddressbuffer-load-misaligned.md -> Examples](../rules/byteaddressbuffer-load-misaligned.md#examples).

## See also

- [Rule page](../rules/byteaddressbuffer-load-misaligned.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
