---
title: "dot4add-opportunity"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: dot4add-opportunity
---

# dot4add-opportunity

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/dot4add-opportunity.md](../rules/dot4add-opportunity.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`dot4add_u8packed` and `dot4add_i8packed` are single instructions on hardware that supports SM 6.4 (DirectX 12 Ultimate): they map to `DP4a` on NVIDIA Turing+, and to `v_dot4_u32_u8` / `v_dot4_i32_i8` on AMD RDNA 2+. The `DP4a` instruction takes two packed 32-bit operands (four `uint8` or `int8` values per operand), computes the four-lane integer dot product, and accumulates the result into a 32-bit integer â€” all in one clock cycle on the integer ALU. The unrolled manual implementation requires 8 mask operations, 4 shift operations, 4 multiplies, and 3 additions for a total of 19 ALU instructions, each taking one integer ALU cycle on RDNA and Turing (though some can dual-issue). The intrinsic replaces all 19 with 1.

## What the rule fires on

A four-tap integer dot product computed manually by unpacking byte-packed values with shifts and masks, multiplying the individual bytes, and summing the products â€” the classic `(a >> 0) & 0xFF) * ((b >> 0) & 0xFF) + ...` pattern for all four byte lanes. The rule fires when all four lanes of two packed `uint` operands are multiplied and accumulated, and the result is a `uint` or `int` accumulation, matching the semantics of `dot4add_u8packed` (SM 6.4) or `dot4add_i8packed`. It also fires on variants where the shift and mask order differs but the mathematical equivalence holds.

See the [What it detects](../rules/dot4add-opportunity.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/dot4add-opportunity.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[dot4add-opportunity.md -> Examples](../rules/dot4add-opportunity.md#examples).

## See also

- [Rule page](../rules/dot4add-opportunity.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
