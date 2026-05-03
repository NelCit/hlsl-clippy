---
title: "pack-then-unpack-roundtrip"
date: 2026-05-02
author: shader-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: pack-then-unpack-roundtrip
---

# pack-then-unpack-roundtrip

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pack-then-unpack-roundtrip.md](../rules/pack-then-unpack-roundtrip.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Both `pack_u8`/`unpack_u8u32` and `f32tof16`/`f16tof32` are ALU instructions. Each costs at least one VALU cycle on RDNA 3 and Turing; the pair together cost two cycles plus a data dependency that prevents the subsequent use of the result from issuing until both complete. In a compute shader that processes large arrays of packed values, a dead round-trip in the inner loop adds two wasted cycles per element per wave. At 32 million elements across a 1080p frame buffer and 64 lanes per wave, the dead pair contributes roughly 1 million wasted VALU instructions per dispatch â€” a measurable fraction of a 60 Hz frame budget.

## What the rule fires on

A `pack_u8(unpack_u8u32(x))` sequence â€” or the signed equivalents `pack_s8(unpack_s8s32(x))` â€” where no arithmetic is performed on the individual channels between the unpack and the repack. The result of the pair is always equal to the original packed value `x`; both operations are dead. The rule also fires on the floating-point analogue: `f16tof32(f32tof16(x))` where the intermediate 16-bit value is not used for any other purpose, because the result of the pair is the FP32 value nearest to `x` that is representable in FP16 â€” a precision loss that serves no purpose if the output is used in a full-precision context.

See the [What it detects](../rules/pack-then-unpack-roundtrip.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pack-then-unpack-roundtrip.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pack-then-unpack-roundtrip.md -> Examples](../rules/pack-then-unpack-roundtrip.md#examples).

## See also

- [Rule page](../rules/pack-then-unpack-roundtrip.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
