---
title: "outputcomplete-missing"
date: 2026-05-02
author: hlsl-clippy maintainers
category: work-graphs
tags: [hlsl, performance, work-graphs]
status: stub
related-rule: outputcomplete-missing
---

# outputcomplete-missing

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/outputcomplete-missing.md](../rules/outputcomplete-missing.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Work graphs (D3D12 introduced in 2024, SM 6.8) are a producer/consumer dispatch model: each node entry point can request output records into a downstream node's input queue, fill those records, and then commit them via `OutputComplete()`. The hardware does not actually issue the downstream node's invocations until the commit happens. If a node obtains output records and exits without calling `OutputComplete()`, the downstream queue is left holding "in-flight" record slots that are neither consumed nor freed; the work-graph scheduler treats them as pending forever. On AMD RDNA 3 (the first hardware to ship work-graph support, via driver-managed scheduling), this manifests as a stalled graph: subsequent dispatches into the affected node hang waiting for queue capacity. On NVIDIA Ada and Blackwell with mesh-node and thread-launch-node support, the same behaviour applies; the node-launch scheduler reserves queue slots that never get released.

## What the rule fires on

Work-graph node entry points that obtain output records via `GetGroupNodeOutputRecords(...)`, `GetThreadNodeOutputRecords(...)`, or the analogous empty-record variants, but do not call `OutputComplete()` on every control-flow path that exits the node. The rule reports both the obvious case (a function that returns without ever calling `OutputComplete`) and the structural case (a node that calls `OutputComplete` inside one branch of an `if` but not on the other branch, or inside a loop body where some iterations may exit without committing).

See the [What it detects](../rules/outputcomplete-missing.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/outputcomplete-missing.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[outputcomplete-missing.md -> Examples](../rules/outputcomplete-missing.md#examples).

## See also

- [Rule page](../rules/outputcomplete-missing.md) -- canonical reference + change log.
- [work-graphs overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
