---
id: mesh-node-not-leaf
category: work-graphs
severity: error
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# mesh-node-not-leaf

> **Pre-v0 status:** this rule page is published ahead of the implementation (preview / experimental, gated behind `[experimental] work-graph-mesh-nodes = true` in `.shader-clippy.toml`). Behaviour described here reflects the design intent; the preview API may change.

*(via ADR 0010)*

## What it detects

A work-graph mesh node (a node with `[NodeLaunch("mesh")]`) that has any outgoing `NodeOutput<...>` declarations. The Mesh Nodes in Work Graphs preview specification requires mesh nodes to be leaf nodes — they consume input records but produce only rasterized output. Slang reflection identifies the node-launch kind and the output declarations; the rule fires when both coexist.

## Why it matters on a GPU

Mesh nodes integrate the mesh-shader pipeline into work graphs: a node receives input records from upstream nodes and emits vertices / primitives to the rasterizer rather than records to a downstream node. On NVIDIA Ada Lovelace and AMD RDNA 3 (the IHVs initially supporting the preview API), the runtime schedules mesh nodes against the rasterizer's input queue exactly as `DispatchMesh` does, but with the work-graph dispatch arguments standing in for the application's `DispatchMesh` arguments.

The leaf-only constraint exists because the runtime has no mechanism to forward records *from* a mesh node to a downstream work-graph node — the mesh node's output goes to the rasterizer, period. Allowing mesh nodes to declare `NodeOutput<...>` would require a brand-new path through the scheduler that does not exist in the current preview implementations and is not in the spec.

The current preview driver path produces a hard PSO-link error when the constraint is violated; spec-status discipline (per ADR 0010) keeps the rule behind an `experimental.work-graph-mesh-nodes` flag because the preview API may change. The diagnostic names the offending output declaration so the author can move it to a non-mesh node or remove it.

## Examples

### Bad

```hlsl
// Mesh node with NodeOutput — leaf-only constraint violated.
struct Record { uint payload; };

[Shader("node")]
[NodeLaunch("mesh")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MeshNode(
    [MaxRecords(64)] DispatchNodeInputRecord<Record> in,
    [MaxRecords(64)] NodeOutput<Record>             downstream, // ERROR: leaf-only
    out vertices Vertex   verts[64],
    out indices  uint3    tris[124])
{
    /* ... */
}
```

### Good

```hlsl
// Mesh node has no NodeOutput; downstream work goes through a separate node.
[Shader("node")]
[NodeLaunch("mesh")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MeshNode(
    [MaxRecords(64)] DispatchNodeInputRecord<Record> in,
    out vertices Vertex   verts[64],
    out indices  uint3    tris[124])
{
    /* ... */
}
```

## Options

This rule is gated behind `[experimental] work-graph-mesh-nodes = true` in `.shader-clippy.toml`. With the gate off, the rule does not fire.

## Fix availability

**none** — Removing the output declaration changes the work-graph topology; the diagnostic names the offending field.

## See also

- Related rule: [mesh-node-missing-output-topology](mesh-node-missing-output-topology.md) — companion mesh-node validation
- Related rule: [mesh-node-uses-vertex-shader-pipeline](mesh-node-uses-vertex-shader-pipeline.md) — companion mesh-node validation
- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — companion work-graph validation
- D3D12 specification: [Mesh nodes in work graphs (preview)](https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/)
- Companion blog post: [work-graphs overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/mesh-node-not-leaf.md)

*© 2026 NelCit, CC-BY-4.0.*
