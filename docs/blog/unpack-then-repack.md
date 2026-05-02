---
title: "unpack-then-repack: An `unpack_u8u32` (or `unpack_s8s32`) call on a value whose four unpacked channels are then…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: unpack-then-repack
---

# unpack-then-repack

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/unpack-then-repack.md](../rules/unpack-then-repack.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`unpack_u8u32` and `pack_u8` are ALU instructions: on RDNA, they map to `v_cvt_pk_u8_f32` and related instructions; on Turing they map to `PRMT`/`BFE`/`BFI` sequences. While individually cheap, the unpack-repack round-trip occupies two ALU instructions per component and introduces a data-flow dependency chain that prevents the scheduler from issuing the surrounding instructions in parallel. For a shader that processes packed colour data in a tight loop, eliminating the dead round-trips reduces the issue-slot pressure and shrinks the number of live intermediates.

## What the rule fires on

An `unpack_u8u32` (or `unpack_s8s32`) call on a value whose four unpacked channels are then repacked with `pack_u8` (or `pack_s8`) without any arithmetic modification to the individual channels in between. The rule also fires on the floating-point analogue: an `f32tof16` conversion immediately followed by `f16tof32` on the same lane, with no operation on the 16-bit integer value between the two calls. In both cases the round-trip is a no-op and both operations can be eliminated: the original packed value is equal to the result of the repack.

See the [What it detects](../rules/unpack-then-repack.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/unpack-then-repack.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[unpack-then-repack.md -> Examples](../rules/unpack-then-repack.md#examples).

## See also

- [Rule page](../rules/unpack-then-repack.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
