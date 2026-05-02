---
title: "quadany-replaceable-with-derivative-uniform-branch"
date: 2026-05-02
author: hlsl-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: quadany-replaceable-with-derivative-uniform-branch
---

# quadany-replaceable-with-derivative-uniform-branch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/quadany-replaceable-with-derivative-uniform-branch.md](../rules/quadany-replaceable-with-derivative-uniform-branch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`QuadAny` (SM 6.7) is the canonical guard for keeping helper-lane participation alive across a per-lane branch in PS: when any lane in the quad wants to enter the branch, all four enter, and the helper lanes provide the derivative neighbours that texture sampling and `ddx`/`ddy` require. The cost is the wave-shuffle that `QuadAny` issues â€” typically 2-4 instructions on NVIDIA Turing/Ada Lovelace, AMD RDNA 2/3, and Intel Xe-HPG.

## What the rule fires on

A `QuadAny(cond)` or `QuadAll(cond)` guard wrapping an `if`-branch whose body is derivative-uniform â€” every operation inside the branch either uses no derivatives or operates on values that are constant across the quad. In that case, the surrounding `if (QuadAny(cond))` adds no quad-correctness benefit and the simpler `if (cond)` is sufficient. The Phase 4 branch-shape detection identifies the wrapper pattern; the data-flow analysis verifies that the body has no derivative-bearing operations on quad-divergent values.

See the [What it detects](../rules/quadany-replaceable-with-derivative-uniform-branch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/quadany-replaceable-with-derivative-uniform-branch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[quadany-replaceable-with-derivative-uniform-branch.md -> Examples](../rules/quadany-replaceable-with-derivative-uniform-branch.md#examples).

## See also

- [Rule page](../rules/quadany-replaceable-with-derivative-uniform-branch.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
