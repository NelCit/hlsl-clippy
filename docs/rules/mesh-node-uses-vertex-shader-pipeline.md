---
id: mesh-node-uses-vertex-shader-pipeline
category: work-graphs
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# mesh-node-uses-vertex-shader-pipeline

> **Pre-v0 status:** this rule page is published ahead of the implementation (preview / experimental, gated behind `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`).

*(via ADR 0010)*

## What it detects

A work-graph node configuration that pairs a mesh node (`[NodeLaunch("mesh")]`) with a vertex-shader pipeline subobject. The Mesh Nodes in Work Graphs preview spec requires the program identifier to reference a Mesh Shader Pipeline State subobject — the analogue of the standalone `D3D12_MESH_SHADER_PIPELINE_STATE_DESC`. The legacy VS+PS pipeline is not a valid container for a mesh node. Slang reflection identifies the pipeline subobject kind referenced by each node entry.

## Why it matters on a GPU

The mesh-shader pipeline on every IHV (Ada / RDNA 3 / Xe-HPG) uses a different state-object path than the legacy IA/VS/HS/DS/GS pipeline. Mesh nodes plug into the mesh-shader path: the runtime resolves the node's pipeline subobject to a `D3D12_MESH_SHADER_PIPELINE_STATE_DESC` and creates the underlying mesh PSO at work-graph state-object creation. The legacy path has no slot for a mesh-node entry — the IA stage is absent in the mesh pipeline, and the work-graph runtime cannot reconstruct a meaningful state for the rasterizer.

PSO creation fails on the preview drivers. The lint catches the mismatch from the shader side: if the entry is `[NodeLaunch("mesh")]` and the pipeline subobject (named in reflection) is not the mesh-pipeline kind, the diagnostic names both ends of the mismatch.

## Examples

### Bad

```hlsl
// Pseudocode (application-side configuration shown as comment for clarity):
// Pipeline subobject for "MyMeshNode" references a graphics PSO with VS+PS,
// not a mesh PSO — preview runtime rejects the state object.
[Shader("node")]
[NodeLaunch("mesh")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MyMeshNode(
    [MaxRecords(64)] DispatchNodeInputRecord<Record> in,
    out vertices Vertex   verts[64],
    out indices  uint3    tris[124])
{
    /* ... */
}
```

### Good

```hlsl
// Application-side: pipeline subobject for "MyMeshNode" is a
// D3D12_MESH_SHADER_PIPELINE_STATE_DESC. Shader entry is unchanged.
[Shader("node")]
[NodeLaunch("mesh")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MyMeshNode(
    [MaxRecords(64)] DispatchNodeInputRecord<Record> in,
    out vertices Vertex   verts[64],
    out indices  uint3    tris[124])
{
    /* ... */
}
```

## Options

This rule is gated behind `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`.

## Fix availability

**none** — The pipeline subobject is defined application-side. The diagnostic names the mismatch so the developer can update the state-object configuration.

## See also

- Related rule: [mesh-node-not-leaf](mesh-node-not-leaf.md) — companion mesh-node validation
- Related rule: [mesh-node-missing-output-topology](mesh-node-missing-output-topology.md) — companion mesh-node validation
- Related rule: [nodeid-implicit-mismatch](nodeid-implicit-mismatch.md) — companion work-graph validation
- D3D12 specification: [Mesh nodes in work graphs (preview)](https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/)
- Companion blog post: [work-graphs overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mesh-node-uses-vertex-shader-pipeline.md)

*© 2026 NelCit, CC-BY-4.0.*
