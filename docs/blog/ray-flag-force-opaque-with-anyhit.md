---
title: "ray-flag-force-opaque-with-anyhit"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: ray-flag-force-opaque-with-anyhit
---

# ray-flag-force-opaque-with-anyhit

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/ray-flag-force-opaque-with-anyhit.md](../rules/ray-flag-force-opaque-with-anyhit.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`RAY_FLAG_FORCE_OPAQUE` skips AnyHit invocation. Binding an AnyHit and then forcing opaque is dead code or a logic bug -- the AnyHit shader will never run. On RDNA 4, NVIDIA Ada/Blackwell and Intel Xe2, the AnyHit binding still costs scheduling table slots even when never invoked.

## What the rule fires on

A `TraceRay(...)` call with `RAY_FLAG_FORCE_OPAQUE` set in a translation unit that also defines a `[shader("anyhit")]` entry point.

See the [What it detects](../rules/ray-flag-force-opaque-with-anyhit.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/ray-flag-force-opaque-with-anyhit.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[ray-flag-force-opaque-with-anyhit.md -> Examples](../rules/ray-flag-force-opaque-with-anyhit.md#examples).

## See also

- [Rule page](../rules/ray-flag-force-opaque-with-anyhit.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
