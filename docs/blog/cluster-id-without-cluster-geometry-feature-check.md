---
title: "cluster-id-without-cluster-geometry-feature-check"
date: 2026-05-02
author: shader-clippy maintainers
category: sm6_10
tags: [hlsl, performance, sm6_10]
status: stub
related-rule: cluster-id-without-cluster-geometry-feature-check
---

# cluster-id-without-cluster-geometry-feature-check

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/cluster-id-without-cluster-geometry-feature-check.md](../rules/cluster-id-without-cluster-geometry-feature-check.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`ClusterID()` is functionally pending on devices that haven't yet shipped the clustered-geometry preview support (Fall 2026 per DirectX dev blog). An unguarded call breaks on older RT-capable devices: drivers may return zero, generate undefined hardware traps, or fall back to a non-clustered BVH path silently.

## What the rule fires on

A call to `ClusterID()` (SM 6.10 ray-tracing intrinsic) without a guarding `IsClusteredGeometrySupported()` check on a path-dominating predicate. Activates only on SM 6.10+ targets.

See the [What it detects](../rules/cluster-id-without-cluster-geometry-feature-check.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/cluster-id-without-cluster-geometry-feature-check.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[cluster-id-without-cluster-geometry-feature-check.md -> Examples](../rules/cluster-id-without-cluster-geometry-feature-check.md#examples).

## See also

- [Rule page](../rules/cluster-id-without-cluster-geometry-feature-check.md) -- canonical reference + change log.
- [sm6_10 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
