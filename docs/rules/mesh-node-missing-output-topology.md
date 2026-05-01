---
id: mesh-node-missing-output-topology
category: work-graphs
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# mesh-node-missing-output-topology

> **Pre-v0 status:** this rule page is published ahead of the implementation (preview / experimental, gated behind `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`).

*(via ADR 0010)*

## What it detects

A mesh node (function annotated `[NodeLaunch("mesh")]`) that lacks the `[outputtopology(...)]` attribute or that has it set to a value other than `"triangle"` / `"line"` (the values supported by the preview specification). Slang reflection identifies the node-launch kind; the rule walks the function attributes and fires on absence or invalid value.

## Why it matters on a GPU

`[outputtopology]` tells the rasterizer how to interpret the index buffer the mesh shader emits — `"triangle"` means three indices per primitive, `"line"` means two. The preview Mesh Nodes spec inherits this attribute requirement directly from the standalone mesh-shader spec; the rasterizer wiring is the same on every IHV (Ada / RDNA 3 / Xe-HPG). Without the attribute, the runtime cannot wire the mesh node to the rasterizer because it doesn't know which primitive-assembly path to use.

The PSO link fails on every preview driver. The DXC validator catches the absence; the lint catches both the absence and the typo / unsupported value cases. The diagnostic names the missing or invalid attribute and lists the supported values.

## Examples

### Bad

```hlsl
// Mesh node missing [outputtopology].
struct Record { uint payload; };

[Shader("node")]
[NodeLaunch("mesh")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(64, 1, 1)]
// missing [outputtopology(...)]
void MeshNode(
    [MaxRecords(64)] DispatchNodeInputRecord<Record> in,
    out vertices Vertex   verts[64],
    out indices  uint3    tris[124])
{
    /* ... */
}
```

### Good

```hlsl
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

This rule is gated behind `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`.

## Fix availability

**none** — The right topology depends on the meshlet's index-buffer layout; the diagnostic names the missing attribute.

## See also

- Related rule: [mesh-node-not-leaf](mesh-node-not-leaf.md) — companion mesh-node validation
- Related rule: [mesh-node-uses-vertex-shader-pipeline](mesh-node-uses-vertex-shader-pipeline.md) — companion mesh-node validation
- D3D12 specification: [Mesh nodes in work graphs (preview)](https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/mesh-node-missing-output-topology.md)

*© 2026 NelCit, CC-BY-4.0.*
