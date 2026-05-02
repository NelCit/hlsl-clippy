---
title: "mesh-node-missing-output-topology: A mesh node (function annotated `[NodeLaunch("mesh")]`) that lacks the `[outputtopology(...)]` attribute or that has…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: mesh-node-missing-output-topology
---

# mesh-node-missing-output-topology

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mesh-node-missing-output-topology.md](../rules/mesh-node-missing-output-topology.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`[outputtopology]` tells the rasterizer how to interpret the index buffer the mesh shader emits — `"triangle"` means three indices per primitive, `"line"` means two. The preview Mesh Nodes spec inherits this attribute requirement directly from the standalone mesh-shader spec; the rasterizer wiring is the same on every IHV (Ada / RDNA 3 / Xe-HPG). Without the attribute, the runtime cannot wire the mesh node to the rasterizer because it doesn't know which primitive-assembly path to use.

## What the rule fires on

A mesh node (function annotated `[NodeLaunch("mesh")]`) that lacks the `[outputtopology(...)]` attribute or that has it set to a value other than `"triangle"` / `"line"` (the values supported by the preview specification). Slang reflection identifies the node-launch kind; the rule walks the function attributes and fires on absence or invalid value.

See the [What it detects](../rules/mesh-node-missing-output-topology.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mesh-node-missing-output-topology.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mesh-node-missing-output-topology.md -> Examples](../rules/mesh-node-missing-output-topology.md#examples).

## See also

- [Rule page](../rules/mesh-node-missing-output-topology.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
