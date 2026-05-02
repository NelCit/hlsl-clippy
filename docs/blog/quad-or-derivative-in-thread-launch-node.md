---
title: "quad-or-derivative-in-thread-launch-node: Work-graph nodes declared with `[NodeLaunch("thread")]` (thread-launch nodes, where each input record produces a single…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: quad-or-derivative-in-thread-launch-node
---

# quad-or-derivative-in-thread-launch-node

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/quad-or-derivative-in-thread-launch-node.md](../rules/quad-or-derivative-in-thread-launch-node.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Work graphs offer three node launch modes: broadcasting (one thread group per record, like compute), coalescing (multiple records folded into one thread group), and thread (one thread per record). The thread-launch mode is the most flexible for fine-grained dispatch but it explicitly does not guarantee the quad-coupled execution that pixel-shader-style derivatives and quad intrinsics require. There is no rasterizer feeding 2x2 quads of co-located lanes; there is no helper-lane mechanism; there is no guarantee that any group of four lanes in a wave correspond to anything spatially or logically related. A `QuadReadAcrossX` issued in a thread-launch node reads from a "quad neighbour" that the runtime has no obligation to make meaningful.

## What the rule fires on

Work-graph nodes declared with `[NodeLaunch("thread")]` (thread-launch nodes, where each input record produces a single thread invocation rather than a thread group) that contain quad-scoped intrinsics (`QuadAny`, `QuadAll`, `QuadReadAcrossX`, `QuadReadAcrossY`, `QuadReadAcrossDiagonal`, `QuadReadLaneAt`), explicit derivatives (`ddx`, `ddy`, `ddx_fine`, `ddx_coarse`, `ddy_fine`, `ddy_coarse`), or implicit-derivative texture sampling (`Texture2D::Sample`, `SampleBias`, `SampleCmp`). All of these require quad-coupled execution that thread-launch nodes do not provide.

See the [What it detects](../rules/quad-or-derivative-in-thread-launch-node.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/quad-or-derivative-in-thread-launch-node.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[quad-or-derivative-in-thread-launch-node.md -> Examples](../rules/quad-or-derivative-in-thread-launch-node.md#examples).

## See also

- [Rule page](../rules/quad-or-derivative-in-thread-launch-node.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
