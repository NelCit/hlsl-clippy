---
id: cluster-id-without-cluster-geometry-feature-check
category: sm6_10
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# cluster-id-without-cluster-geometry-feature-check

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A call to `ClusterID()` (SM 6.10 ray-tracing intrinsic) without a guarding
`IsClusteredGeometrySupported()` check on a path-dominating predicate.
Activates only on SM 6.10+ targets.

## Why it matters on a GPU

`ClusterID()` is functionally pending on devices that haven't yet shipped the
clustered-geometry preview support (Fall 2026 per DirectX dev blog). An
unguarded call breaks on older RT-capable devices: drivers may return zero,
generate undefined hardware traps, or fall back to a non-clustered BVH path
silently.

## Examples

### Bad

```hlsl
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) { uint id = ClusterID(); }
```

### Good

```hlsl
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    if (IsClusteredGeometrySupported()) { uint id = ClusterID(); }
}
```

## Options

none

## Fix availability

**suggestion** — Add a feature-availability check.
