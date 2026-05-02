---
title: "pipeline-when-inline-better"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: pipeline-when-inline-better
---

# pipeline-when-inline-better

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/pipeline-when-inline-better.md](../rules/pipeline-when-inline-better.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

DXR exposes two traversal styles. *Pipeline ray tracing* uses `TraceRay` and a state-object full of hit-group and miss shaders; the runtime's scheduler dispatches the right shader through the shader-binding-table indirection on every hit, and SER (SM 6.9) layers on top to coalesce the divergent dispatches. *Inline ray tracing* uses `RayQuery` and runs the traversal as a method call inside the caller â€” no shader table, no hit-group dispatch, no payload spill across the trace. Both NVIDIA Ada Lovelace and AMD RDNA 3 expose the same RT-core hardware to both modes; the difference is entirely in the surrounding scheduling.

## What the rule fires on

A full DXR pipeline-`TraceRay` call from a stage that would do better with an inline `RayQuery`. The rule fires on `TraceRay` invocations whose payload is empty or single-scalar, whose `MissShaderIndex` and `RayContributionToHitGroupIndex` resolve to "shadow-ray" hit groups (any-hit / closest-hit shaders that only set a single bool), and whose call-site stage already runs in compute or pixel â€” both stages where a `RayQuery<RAY_FLAG_*>` inline traversal pays no shader-table indirection. The companion rule `inline-rayquery-when-pipeline-better` detects the opposite direction.

See the [What it detects](../rules/pipeline-when-inline-better.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/pipeline-when-inline-better.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[pipeline-when-inline-better.md -> Examples](../rules/pipeline-when-inline-better.md#examples).

## See also

- [Rule page](../rules/pipeline-when-inline-better.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
