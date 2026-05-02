---
title: "pack-clamp-on-prove-bounded: A call to `pack_clamp_u8` (or `pack_clamp_s8`) where the argument can be proven to already…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: pack-clamp-on-prove-bounded
---

# pack-clamp-on-prove-bounded

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pack-clamp-on-prove-bounded.md](../rules/pack-clamp-on-prove-bounded.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`pack_clamp_u8` includes an implicit per-component clamp before packing. On RDNA, this maps to `v_cvt_pk_u8_f32` with the clamp modifier set, or to an extra `v_min_u32`/`v_max_u32` pair applied to the integer operands before the byte-packing instruction. On Turing, it maps to `VMIN`/`VMAX` instructions preceding `PRMT`. When the inputs are already bounded, these clamp instructions execute and consume ALU slots while producing a result identical to what `pack_u8` would produce without the clamp. Replacing `pack_clamp_u8` with `pack_u8` in the bounded case eliminates one instruction per component (up to four instructions removed for a `uint4` argument).

## What the rule fires on

A call to `pack_clamp_u8` (or `pack_clamp_s8`) where the argument can be proven to already lie within the clamped range. Specifically: when the argument is the result of `(uint4)(saturate(v) * 255.0)` or an equivalent expression that passes through `saturate`, `clamp(..., 0, 1)`, or a `min`/`max` pair bounding the value to [0, 1] before scaling to [0, 255], the clamping inside `pack_clamp_u8` is provably a no-op. The rule fires when value range analysis can establish that each component of the `uint4` argument is in [0, 255] on all execution paths.

See the [What it detects](../rules/pack-clamp-on-prove-bounded.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pack-clamp-on-prove-bounded.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pack-clamp-on-prove-bounded.md -> Examples](../rules/pack-clamp-on-prove-bounded.md#examples).

## See also

- [Rule page](../rules/pack-clamp-on-prove-bounded.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
