---
title: "firstbit-vs-log2-trick"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: firstbit-vs-log2-trick
---

# firstbit-vs-log2-trick

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/firstbit-vs-log2-trick.md](../rules/firstbit-vs-log2-trick.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`firstbithigh(uint)` and `firstbitlow(uint)` are both single ISA instructions on every shader-capable GPU since SM 5.0. AMD RDNA 3 lowers them to `v_ffbh_u32` and `v_ffbl_b32`, both full-rate VALU integer ops. NVIDIA Turing and Ada Lovelace use `FLO` (find-leading-one) and a related instruction; both retire in a single cycle on the integer pipe. The `log2` detour, by contrast, requires three steps: an integer-to-float conversion (`v_cvt_f32_u32` on RDNA, full-rate but with a separate ALU port), a `log2` (`v_log_f32` on RDNA 3, which issues at one-quarter rate as a transcendental on the SFU/MUFU shared with `exp`, `rcp`, `rsqrt`, `sin`, and `cos`), and a float-to-integer truncation back. The end-to-end cost is roughly 6-8 VALU-equivalent cycles versus one for the intrinsic â€” and the slow step is exactly the transcendental unit that the rest of the shader is also competing for.

## What the rule fires on

Expressions that compute the position of the most-significant set bit of a non-zero unsigned integer using a `log2` / float-cast detour, in any of the common shapes: `(uint)log2((float)x)`, `(int)log2((float)x)`, `floor(log2((float)x))`, or the asfloat-asuint exponent-extraction trick `(asuint((float)x) >> 23) - 127`. The rule also matches the symmetric low-bit form built around `log2` of `x & -x`. The match keys on a `log2` (or `asuint` exponent extraction) applied to a value that has been freshly cast or coerced from an unsigned integer, with the result then truncated back to an integer. It does not fire when the surrounding code uses the floating-point logarithm value itself for anything other than re-truncation.

See the [What it detects](../rules/firstbit-vs-log2-trick.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/firstbit-vs-log2-trick.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[firstbit-vs-log2-trick.md -> Examples](../rules/firstbit-vs-log2-trick.md#examples).

## See also

- [Rule page](../rules/firstbit-vs-log2-trick.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
