---
title: "flatten-on-uniform-branch"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: flatten-on-uniform-branch
---

# flatten-on-uniform-branch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/flatten-on-uniform-branch.md](../rules/flatten-on-uniform-branch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The `[flatten]` and `[branch]` attributes are HLSL hints that tell the compiler how to lower an `if` / `else` to GPU instructions. `[flatten]` says: evaluate both arms unconditionally and select between them with a predicate (no jump, no divergence handling â€” both arms always run). `[branch]` says: emit a real conditional jump and let the lanes that take the false path skip the true arm's instructions entirely. The two have very different cost models: `[flatten]` is cheaper when the arms are short and the branch would otherwise cost more in divergence handling than the work the skipped arm performs; `[branch]` is cheaper when the arms are non-trivial and the predicate is uniform across the wave (so all lanes take the same path and the inactive arm's instructions are genuinely skipped).

## What the rule fires on

The `[flatten]` attribute applied to an `if` / `else` whose condition is dynamically uniform across the wave (or thread group, depending on stage). Uniformity is established by the rule's existing wave-uniformity analysis: cbuffer scalars, constants, `WaveReadLaneFirst` results, and any expression provably independent of `SV_DispatchThreadID` / `SV_GroupThreadID` / per-lane attribute interpolation. Shares the uniformity machinery with the locked `branch-on-uniform-missing-attribute` rule.

See the [What it detects](../rules/flatten-on-uniform-branch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/flatten-on-uniform-branch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[flatten-on-uniform-branch.md -> Examples](../rules/flatten-on-uniform-branch.md#examples).

## See also

- [Rule page](../rules/flatten-on-uniform-branch.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
