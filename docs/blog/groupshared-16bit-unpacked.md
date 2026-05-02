---
title: "groupshared-16bit-unpacked: A `groupshared` array whose element type is `min16float`, `min16uint`, `min16int`, `float16_t`, `uint16_t`, or `int16_t`,…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-16bit-unpacked
---

# groupshared-16bit-unpacked

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-16bit-unpacked.md](../rules/groupshared-16bit-unpacked.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

AMD RDNA 2/3 packs two 16-bit values per VGPR lane and per LDS bank, but the packing only pays off when the entire data path stays narrow. The hardware exposes packed-math instructions (`v_pk_add_f16`, `v_pk_mul_f16`, `v_dot2_f16`) that consume two `float16` lanes per VGPR and produce two `float16` results in one issue slot. When source code stores `min16float` in groupshared but widens to `float` at the load site, the savings collapse: the LDS access still moves the narrow representation, but the immediately-following type promotion forces the value into a full-width VGPR before any arithmetic, and the compiler cannot recover the narrow packing without proving every consumer remains narrow. NVIDIA Turing introduced HFMA2 and Ada extends it; the same principle holds — the half2 / int16x2 instructions need narrow operands sitting in narrow registers, not promoted scalars.

## What the rule fires on

A `groupshared` array whose element type is `min16float`, `min16uint`, `min16int`, `float16_t`, `uint16_t`, or `int16_t`, where every load site widens the value to 32 bits before any arithmetic. The detector uses Slang reflection to confirm the groupshared element width and the AST to confirm that no consuming expression uses a packed-math intrinsic (`dot2add`, `mul`/`mad` on a `min16float2` operand kept narrow, etc.). It fires when the storage is narrow but every consumer is wide. It does not fire when at least one consuming site keeps the value in 16 bits through a packed intrinsic.

See the [What it detects](../rules/groupshared-16bit-unpacked.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-16bit-unpacked.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-16bit-unpacked.md -> Examples](../rules/groupshared-16bit-unpacked.md#examples).

## See also

- [Rule page](../rules/groupshared-16bit-unpacked.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
