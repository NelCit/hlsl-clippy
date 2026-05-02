---
title: "min16float-opportunity"
date: 2026-05-02
author: hlsl-clippy maintainers
category: packed-math
tags: [hlsl, performance, packed-math]
status: stub
related-rule: min16float-opportunity
---

# min16float-opportunity

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/min16float-opportunity.md](../rules/min16float-opportunity.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`min16float` maps to FP16 on AMD RDNA and NVIDIA Turing and later. On RDNA 3, the shader ALU supports native FP16 arithmetic at the same throughput as FP32 in packed form: two FP16 operations can be issued in one FP32 VALU clock using `v_pk_*_f16` instructions. For a shader that is ALU-limited and operates predominantly on colour values, converting the inner loop to `min16float` can double effective VALU throughput without changing the wave count.

## What the rule fires on

ALU-bound regions in a shader where all values in a computation chain are `float` (32-bit) but the precision requirement is consistent with `min16float` (minimum 16-bit): the inputs are either in the normalised range [0, 1] (colour channels, barycentric weights, UV coordinates), or the computation is an accumulation whose intermediate error budget allows 16-bit rounding. The rule fires when it can establish â€” either from value-range analysis or from explicit `saturate`/clamp annotations â€” that no intermediate value in the chain exceeds the `min16float` representable range and that the output is consumed by a write to an 8-bit render target, a sampler-feedback write, or an explicit conversion back to a packed format.

See the [What it detects](../rules/min16float-opportunity.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/min16float-opportunity.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[min16float-opportunity.md -> Examples](../rules/min16float-opportunity.md#examples).

## See also

- [Rule page](../rules/min16float-opportunity.md) -- canonical reference + change log.
- [packed-math overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
