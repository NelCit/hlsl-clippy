---
title: "nodeid-implicit-mismatch"
date: 2026-05-02
author: shader-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: nodeid-implicit-mismatch
---

# nodeid-implicit-mismatch

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/nodeid-implicit-mismatch.md](../rules/nodeid-implicit-mismatch.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Work graphs (SM 6.8) replace the static compute dispatch graph with a runtime-driven node graph. Each node is a shader entry point with a `[NodeLaunch("...")]` attribute, and edges are declared on the producer side via `NodeOutput<RecordType>` parameters. The runtime resolves edges by `NodeID` (a `(name, index)` pair), and the resolved graph is baked into a Work Graph state object at PSO time. On NVIDIA Ada Lovelace and AMD RDNA 3 (the two IHVs that initially shipped work-graph drivers), the scheduler stores the resolved adjacency in an on-chip table; an unresolved edge means a producer has nowhere to put its records, which the runtime treats as a hard validation error.

## What the rule fires on

A work-graph node entry where the node's implicit `[NodeID(...)]` (derived from the function name when no explicit attribute is present) does not match the `NodeID` referenced by a producer's `NodeOutput<...>` declaration. The rule uses Slang reflection to enumerate node entry points, reconciles the declared / implicit `[NodeID(name, index)]` of each, and matches them against the node identifiers referenced from `EmptyNodeOutput`, `NodeOutput`, `NodeOutputArray`, and `EmptyNodeOutputArray` declarations on producer nodes. Mismatches fire as errors because the work-graph linker rejects them at PSO creation.

See the [What it detects](../rules/nodeid-implicit-mismatch.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/nodeid-implicit-mismatch.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[nodeid-implicit-mismatch.md -> Examples](../rules/nodeid-implicit-mismatch.md#examples).

## See also

- [Rule page](../rules/nodeid-implicit-mismatch.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
