---
title: "countbits-vs-manual-popcount: Loops or expression trees that count the set bits of an integer scalar by…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: countbits-vs-manual-popcount
---

# countbits-vs-manual-popcount

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/countbits-vs-manual-popcount.md](../rules/countbits-vs-manual-popcount.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Every shader-capable GPU shipping since DirectX 11 has a single-cycle population-count instruction at the ISA level, and HLSL exposes it as `countbits(uint)`. On AMD RDNA 3 it lowers to `v_bcnt_u32_b32`, a full-rate VALU op that issues at one result per SIMD32 lane per clock. NVIDIA Turing and Ada Lovelace expose `POPC` as a single SASS instruction with similar throughput on the integer pipe. Intel Xe-HPG provides `cbit` as a one-cycle integer ALU op. Replacing a 32-iteration loop with one of these instructions is not a small win: it is a 32x reduction in dynamic instruction count for the worst-case input and an unbounded reduction in branch-divergence overhead, because the loop's exit condition depends on the popcount value and therefore varies across the wave.

## What the rule fires on

Loops or expression trees that count the set bits of an integer scalar by hand, in any of the canonical forms: a `for`/`while` loop that shifts and accumulates the low bit (`while (x) { count += x & 1; x >>= 1; }`), Brian Kernighan's clear-the-lowest-bit loop (`while (x) { x &= x - 1; ++count; }`), an unrolled SWAR-style sequence of mask-shift-add reductions (`x = (x & 0x55555555u) + ((x >> 1) & 0x55555555u);` and the matching widening passes), or a small lookup-table indexed by an 8- or 16-bit slice. The rule keys on the structural shape — a loop body that strictly tests, masks, and decrements a working integer, or a sequence of three or four magic-constant masks at 0x55555555/0x33333333/0x0F0F0F0F. It does not fire when the loop body does anything besides bit counting (e.g., remembers which bits were set, or maps each bit to a side effect).

See the [What it detects](../rules/countbits-vs-manual-popcount.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/countbits-vs-manual-popcount.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[countbits-vs-manual-popcount.md -> Examples](../rules/countbits-vs-manual-popcount.md#examples).

## See also

- [Rule page](../rules/countbits-vs-manual-popcount.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
