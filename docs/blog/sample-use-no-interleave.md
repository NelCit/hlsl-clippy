---
title: "sample-use-no-interleave"
date: 2026-05-02
author: shader-clippy maintainers
category: memory
tags: [hlsl, performance, memory]
status: stub
related-rule: sample-use-no-interleave
---

# sample-use-no-interleave

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/sample-use-no-interleave.md](../rules/sample-use-no-interleave.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

NVIDIA Nsight surfaces this pattern as "Warp Stalled by L1 Long Scoreboard" -- texture fetches without enough work between sample and use cause warp stalls when the L1 cache misses (~150-300 cycles on Turing/Ada/Blackwell; ~120-200 on RDNA 2-4). Interleaving compute (or other independent work) between sample and use lets the scheduler hide the latency.

## What the rule fires on

A `Texture.Sample*()` call whose result is consumed within the next 3 statements without intervening compute. The default heuristic uses a 3-statement sliding window over the enclosing compound block.

See the [What it detects](../rules/sample-use-no-interleave.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/sample-use-no-interleave.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[sample-use-no-interleave.md -> Examples](../rules/sample-use-no-interleave.md#examples).

## See also

- [Rule page](../rules/sample-use-no-interleave.md) -- canonical reference + change log.
- [memory overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
