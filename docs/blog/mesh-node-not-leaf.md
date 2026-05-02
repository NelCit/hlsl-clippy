---
title: "mesh-node-not-leaf"
date: 2026-05-02
author: hlsl-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: mesh-node-not-leaf
---

# mesh-node-not-leaf

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mesh-node-not-leaf.md](../rules/mesh-node-not-leaf.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Mesh nodes integrate the mesh-shader pipeline into work graphs: a node receives input records from upstream nodes and emits vertices / primitives to the rasterizer rather than records to a downstream node. On NVIDIA Ada Lovelace and AMD RDNA 3 (the IHVs initially supporting the preview API), the runtime schedules mesh nodes against the rasterizer's input queue exactly as `DispatchMesh` does, but with the work-graph dispatch arguments standing in for the application's `DispatchMesh` arguments.

## What the rule fires on

A work-graph mesh node (a node with `[NodeLaunch("mesh")]`) that has any outgoing `NodeOutput<...>` declarations. The Mesh Nodes in Work Graphs preview specification requires mesh nodes to be leaf nodes â€” they consume input records but produce only rasterized output. Slang reflection identifies the node-launch kind and the output declarations; the rule fires when both coexist.

See the [What it detects](../rules/mesh-node-not-leaf.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mesh-node-not-leaf.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mesh-node-not-leaf.md -> Examples](../rules/mesh-node-not-leaf.md#examples).

## See also

- [Rule page](../rules/mesh-node-not-leaf.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
