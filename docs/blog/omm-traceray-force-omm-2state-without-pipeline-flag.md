---
title: "omm-traceray-force-omm-2state-without-pipeline-flag"
date: 2026-05-02
author: shader-clippy maintainers
category: opacity-micromaps
tags: [hlsl, performance, opacity-micromaps]
status: stub
related-rule: omm-traceray-force-omm-2state-without-pipeline-flag
---

# omm-traceray-force-omm-2state-without-pipeline-flag

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/omm-traceray-force-omm-2state-without-pipeline-flag.md](../rules/omm-traceray-force-omm-2state-without-pipeline-flag.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The DXR 1.2 OMM specification has a project-level gate (`D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS`) and a per-trace gate (the ray flag of the same name). Both must be set for OMM consultation to happen on a given trace. The pipeline flag is set in the state-object's pipeline-config subobject and reflects the application's promise to the runtime that OMM blocks may be present in the BVH; the per-trace flag is the request.

## What the rule fires on

A `TraceRay(...)` call with `RAY_FLAG_FORCE_OMM_2_STATE` set when the DXR pipeline subobject's `D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS` is not set. The rule reads the pipeline-flags subobject through Slang reflection and compares it against the constant-folded ray flags at the trace site; mismatch fires.

See the [What it detects](../rules/omm-traceray-force-omm-2state-without-pipeline-flag.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/omm-traceray-force-omm-2state-without-pipeline-flag.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[omm-traceray-force-omm-2state-without-pipeline-flag.md -> Examples](../rules/omm-traceray-force-omm-2state-without-pipeline-flag.md#examples).

## See also

- [Rule page](../rules/omm-traceray-force-omm-2state-without-pipeline-flag.md) -- canonical reference + change log.
- [opacity-micromaps overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
