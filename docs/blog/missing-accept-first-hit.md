---
title: "missing-accept-first-hit"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: missing-accept-first-hit
---

# missing-accept-first-hit

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/missing-accept-first-hit.md](../rules/missing-accept-first-hit.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Without `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`, the BVH traversal must continue until every leaf node intersected by the ray has been tested, because any later intersection might be closer than the current candidate. With the flag set, traversal terminates the moment the first opaque hit is recorded â€” for shadow rays, this halves the average traversal cost in dense geometry and reduces it by far more in skybox-bounded scenes where the typical shadow ray passes through hundreds of empty BVH nodes before hitting an occluder.

## What the rule fires on

`TraceRay` (or `RayQuery::TraceRayInline`) call sites whose ray-flags argument lacks `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` even though the caller does not actually use the closest-hit information. The rule performs IR-level liveness and use-def analysis on the payload after the trace returns: if every field of the payload that is read post-trace is either (a) written only by the miss shader, or (b) a single `bool`/`uint` "did we hit anything" flag, the rule fires. The intent inferred by the rule is that the trace is a visibility / shadow / occlusion query, not a true closest-hit lookup.

See the [What it detects](../rules/missing-accept-first-hit.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/missing-accept-first-hit.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[missing-accept-first-hit.md -> Examples](../rules/missing-accept-first-hit.md#examples).

## See also

- [Rule page](../rules/missing-accept-first-hit.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
