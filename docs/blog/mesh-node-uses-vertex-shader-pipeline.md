---
title: "mesh-node-uses-vertex-shader-pipeline: A work-graph node configuration that pairs a mesh node (`[NodeLaunch("mesh")]`) with a vertex-shader pipeline…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: mesh-node-uses-vertex-shader-pipeline
---

# mesh-node-uses-vertex-shader-pipeline

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/mesh-node-uses-vertex-shader-pipeline.md](../rules/mesh-node-uses-vertex-shader-pipeline.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The mesh-shader pipeline on every IHV (Ada / RDNA 3 / Xe-HPG) uses a different state-object path than the legacy IA/VS/HS/DS/GS pipeline. Mesh nodes plug into the mesh-shader path: the runtime resolves the node's pipeline subobject to a `D3D12_MESH_SHADER_PIPELINE_STATE_DESC` and creates the underlying mesh PSO at work-graph state-object creation. The legacy path has no slot for a mesh-node entry — the IA stage is absent in the mesh pipeline, and the work-graph runtime cannot reconstruct a meaningful state for the rasterizer.

## What the rule fires on

A work-graph node configuration that pairs a mesh node (`[NodeLaunch("mesh")]`) with a vertex-shader pipeline subobject. The Mesh Nodes in Work Graphs preview spec requires the program identifier to reference a Mesh Shader Pipeline State subobject — the analogue of the standalone `D3D12_MESH_SHADER_PIPELINE_STATE_DESC`. The legacy VS+PS pipeline is not a valid container for a mesh node. Slang reflection identifies the pipeline subobject kind referenced by each node entry.

See the [What it detects](../rules/mesh-node-uses-vertex-shader-pipeline.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/mesh-node-uses-vertex-shader-pipeline.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[mesh-node-uses-vertex-shader-pipeline.md -> Examples](../rules/mesh-node-uses-vertex-shader-pipeline.md#examples).

## See also

- [Rule page](../rules/mesh-node-uses-vertex-shader-pipeline.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
