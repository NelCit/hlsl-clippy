---
title: "wave-reduction-pixel-without-helper-attribute"
date: 2026-05-02
author: shader-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: wave-reduction-pixel-without-helper-attribute
---

# wave-reduction-pixel-without-helper-attribute

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wave-reduction-pixel-without-helper-attribute.md](../rules/wave-reduction-pixel-without-helper-attribute.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

By default, pixel-shader wave intrinsics exclude helper lanes from the active mask: a `WaveActiveSum(x)` that sees a partially-covered quad sums only the covered lanes and ignores the helpers. This is usually what the author wants â€” helpers don't contribute meaningful values for non-derivative work. But when the reduction's result then flows into a derivative operation, the derivative computation needs the full quad to be coherent: `ddx(uniform)` is zero only when *all four* quad lanes have the same value.

## What the rule fires on

A pixel-shader entry that performs a wave reduction (`WaveActiveSum`, `WaveActiveProduct`, `WaveActiveCountBits`, `WaveActiveBallot`, etc.) whose result then flows into a derivative-bearing operation (`ddx`, `ddy`, an implicit-derivative texture sample) without `[WaveOpsIncludeHelperLanes]` declared on the entry. The Phase 4 data-flow analysis traces the reduction's output to the derivative operation; the rule fires when both occur and the attribute is absent.

See the [What it detects](../rules/wave-reduction-pixel-without-helper-attribute.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wave-reduction-pixel-without-helper-attribute.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wave-reduction-pixel-without-helper-attribute.md -> Examples](../rules/wave-reduction-pixel-without-helper-attribute.md#examples).

## See also

- [Rule page](../rules/wave-reduction-pixel-without-helper-attribute.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
