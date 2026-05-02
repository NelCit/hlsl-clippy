---
id: nodeid-implicit-mismatch
category: work-graphs
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# nodeid-implicit-mismatch

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

A work-graph node entry where the node's implicit `[NodeID(...)]` (derived from the function name when no explicit attribute is present) does not match the `NodeID` referenced by a producer's `NodeOutput<...>` declaration. The rule uses Slang reflection to enumerate node entry points, reconciles the declared / implicit `[NodeID(name, index)]` of each, and matches them against the node identifiers referenced from `EmptyNodeOutput`, `NodeOutput`, `NodeOutputArray`, and `EmptyNodeOutputArray` declarations on producer nodes. Mismatches fire as errors because the work-graph linker rejects them at PSO creation.

## Why it matters on a GPU

Work graphs (SM 6.8) replace the static compute dispatch graph with a runtime-driven node graph. Each node is a shader entry point with a `[NodeLaunch("...")]` attribute, and edges are declared on the producer side via `NodeOutput<RecordType>` parameters. The runtime resolves edges by `NodeID` (a `(name, index)` pair), and the resolved graph is baked into a Work Graph state object at PSO time. On NVIDIA Ada Lovelace and AMD RDNA 3 (the two IHVs that initially shipped work-graph drivers), the scheduler stores the resolved adjacency in an on-chip table; an unresolved edge means a producer has nowhere to put its records, which the runtime treats as a hard validation error.

Implicit `NodeID` resolution makes this footgun easy to hit: HLSL allows omitting `[NodeID]`, in which case the function name is the node ID and the index defaults to 0. A producer that writes to `NodeOutput<MyRecord> ChildNode` is implicitly looking for a node named `ChildNode` with index 0; if the consumer entry is named `ChildNodeImpl` (a refactor leftover) or carries an explicit `[NodeID("ChildNode", 1)]`, the edge does not resolve. PSO creation fails with a generic "node not found" error that names the producer but does not always point clearly at the consumer typo.

Surfacing the mismatch at lint time turns a confusing PSO-link error into a diagnostic that names both ends of the edge and points at the producer field that didn't resolve. On Intel Xe-HPG, work-graph driver support is still maturing; the lint becomes more valuable there because runtime error messages are even less specific than on the established IHVs.

## Examples

### Bad

```hlsl
// Producer references "Process" but the consumer is named "ProcessRecord".
struct Record { uint payload; };

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(64, 1, 1)]
[numthreads(32, 1, 1)]
void Producer(
    [MaxRecords(64)] NodeOutput<Record> Process // looks for Process,0
) {
    /* ... */
}

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
void ProcessRecord([MaxRecords(64)] GroupNodeInputRecords<Record> input) {
    // implicit [NodeID("ProcessRecord", 0)] — does not match.
}
```

### Good

```hlsl
// Either rename the consumer or add an explicit [NodeID].
[Shader("node")]
[NodeLaunch("coalescing")]
[NodeID("Process", 0)]   // explicit ID matches producer reference
[NumThreads(32, 1, 1)]
void ProcessRecord([MaxRecords(64)] GroupNodeInputRecords<Record> input) {
    /* ... */
}
```

## Options

none

## Fix availability

**none** — The right side of the rename (producer or consumer) is an authorial decision the linter cannot make. The diagnostic enumerates the unresolved edges and the candidate consumers (case-insensitive substring matches against the producer reference).

## See also

- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — mandatory `OutputComplete` calls on node outputs
- Related rule: [quad-or-derivative-in-thread-launch-node](quad-or-derivative-in-thread-launch-node.md) — derivative use in thread-launch node
- D3D12 specification: Work Graphs node identification (SM 6.8) and `[NodeID]` attribute
- Companion blog post: [work-graphs overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/nodeid-implicit-mismatch.md)

*© 2026 NelCit, CC-BY-4.0.*
